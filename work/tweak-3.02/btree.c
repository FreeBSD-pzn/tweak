/*
 * Flexible B-tree implementation. Supports reference counting for
 * copy-on-write, user-defined node properties, and variable
 * degree.
 * 
 * This file is copyright 2001,2004 Simon Tatham.
 * 
 * Permission is hereby granted, free of charge, to any person
 * obtaining a copy of this software and associated documentation
 * files (the "Software"), to deal in the Software without
 * restriction, including without limitation the rights to use,
 * copy, modify, merge, publish, distribute, sublicense, and/or
 * sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following
 * conditions:
 *
 * The above copyright notice and this permission notice shall be
 * included in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES
 * OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT.  IN NO EVENT SHALL SIMON TATHAM BE LIABLE FOR
 * ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF
 * CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

/*
 * TODO:
 * 
 * Possibly TODO in future, but may not be sensible in this code
 * architecture:
 * 
 *  - user write properties.
 *     * this all happens during write_unlock(), I think. Except
 *       that we'll now need an _internal_ write_unlock() which
 *       does everything except user write properties. Sigh.
 *     * note that we also need a transform function for elements
 *       (rot13 will certainly require this, and reverse will
 *       require it if the elements themselves are in some way
 *       reversible).
 * 
 * Still untested:
 *  - searching on user read properties.
 *  - user-supplied copy function.
 *  - bt_add when element already exists.
 *  - bt_del when element doesn't.
 *  - splitpos with before==TRUE.
 *  - split() on sorted elements (but it should be fine).
 *  - bt_replace, at all (it won't be useful until we get user read
 *    properties).
 *  - bt_index_w (won't make much sense until we start using
 *    user-supplied copy fn).
 */

#include <stdlib.h>
#include <string.h>
#include <assert.h>

#ifdef TEST
#include <stdio.h>
#include <stdarg.h>
#endif

#include "btree.h"

#ifdef TEST
static void set_invalid_property(void *prop);
#endif

/* ----------------------------------------------------------------------
 * Type definitions.
 */

typedef union nodecomponent nodecomponent;
typedef nodecomponent *nodeptr;

/*
 * For type-checking purposes, and to ensure I don't accidentally
 * confuse node_addr with node_ptr during implementation, I'll
 * define node_addr for the in-memory case as being a struct
 * containing only a nodeptr.
 * 
 * This unfortunately needs to go in btree.h so that clients
 * writing user properties can know about the nodecomponent
 * structure.
 */
typedef struct {
    nodeptr p;
} node_addr;

/*
 * A B-tree node is a horrible thing when you're trying to be
 * flexible. It is of variable size, and it contains a variety of
 * distinct types of thing: nodes, elements, some counters, some
 * user-defined properties ... it's a horrible thing. So we define
 * it as an array of unions, each union being either an `int' or a
 * `bt_element_t' or a `node_addr'...
 */

union nodecomponent {
    int i;
    node_addr na;
    bt_element_t ep;
};

static const node_addr NODE_ADDR_NULL = { NULL };

/*
 * The array of nodecomponents will take the following form:
 * 
 *  - (maxdegree) child pointers.
 *  - (maxdegree-1) element pointers.
 *  - one subtree count (current number of child pointers that are
 *    valid; note that `valid' doesn't imply non-NULL).
 *  - one element count.
 *  - one reference count.
 */

struct btree {
    int mindegree;		       /* min number of subtrees */
    int maxdegree;		       /* max number of subtrees */
    int depth;			       /* helps to store this explicitly */
    node_addr root;
    cmpfn_t cmp;
    copyfn_t copy;
    freefn_t freeelt;
    int propsize, propalign, propoffset;
    propmakefn_t propmake;
    propmergefn_t propmerge;
    void *userstate;		       /* passed to all user functions */
};

/* ----------------------------------------------------------------------
 * Memory management routines and other housekeeping.
 */
#ifdef HAVE_ALLOCA
#  define ialloc(x) alloca(x)
#  define ifree(x)
#else
#  define ialloc(x) smalloc(x)
#  define ifree(x) sfree(x)
#endif

#define new1(t) ( (t *) smalloc(sizeof(t)) )
#define newn(t, n) ( (t *) smalloc((n) * sizeof(t)) )
#define inew1(t) ( (t *) ialloc(sizeof(t)) )
#define inewn(t, n) ( (t *) ialloc((n) * sizeof(t)) )

static void *smalloc(size_t size)
{
    void *ret = malloc(size);
    if (!ret)
	abort();
    return ret;
}

static void sfree(void *p)
{
    free(p);
}

#ifndef FALSE
#define FALSE 0
#endif
#ifndef TRUE
#define TRUE 1
#endif

/* We could probably do with more compiler-specific branches of this #if. */
#if defined(__GNUC__)
#define INLINE __inline
#else
#define INLINE
#endif

/* Hooks into the low-level code for test purposes. */
#ifdef TEST
void testlock(int write, int set, nodeptr n);
#else
#define testlock(w,s,n)
#endif

/* ----------------------------------------------------------------------
 * Low-level helper routines, which understand the in-memory format
 * of a node and know how to read-lock and write-lock.
 */

/*
 * Read and write the node_addr of a child.
 */
static INLINE node_addr bt_child(btree *bt, nodeptr n, int index)
{
    return n[index].na;
}
static INLINE void bt_set_child(btree *bt, nodeptr n,
				int index, node_addr value)
{
    n[index].na = value;
}

/*
 * Read and write the address of an element.
 */
static INLINE bt_element_t bt_element(btree *bt, nodeptr n, int index)
{
    return n[bt->maxdegree + index].ep;
}
static INLINE void bt_set_element(btree *bt, nodeptr n,
				  int index, bt_element_t value)
{
    n[bt->maxdegree + index].ep = value;
}

/*
 * Give the number of subtrees currently present in an element.
 */
static INLINE int bt_subtrees(btree *bt, nodeptr n)
{
    return n[bt->maxdegree*2-1].i;
}
#define bt_elements(bt,n) (bt_subtrees(bt,n) - 1)

/*
 * Give the minimum and maximum number of subtrees allowed in a
 * node.
 */
static INLINE int bt_min_subtrees(btree *bt)
{
    return bt->mindegree;
}
static INLINE int bt_max_subtrees(btree *bt)
{
    return bt->maxdegree;
}

/*
 * Return the count of items, and the user properties, in a
 * particular subtree of a node.
 * 
 * Note that in the in-memory form of the tree, this breaks the
 * read-locking semantics, by reading the counts out of the child
 * nodes without bothering to lock them. We're allowed to do this
 * because this function is implemented at the same very low level
 * as the implementation of bt_read_lock(), so we're allowed to
 * know that read locking actually doesn't do anything.
 */
static INLINE int bt_child_count(btree *bt, nodeptr n, int index)
{
    if (n[index].na.p)
	return n[index].na.p[bt->maxdegree*2].i;
    else
	return 0;
}

static INLINE void *bt_child_prop(btree *bt, nodeptr n, int index)
{
    if (n[index].na.p)
	return (char *)n[index].na.p + bt->propoffset;
    else
	return NULL;
}

/*
 * Return the count of items in a whole node.
 */
static INLINE int bt_node_count(btree *bt, nodeptr n)
{
    return n[bt->maxdegree*2].i;
}

/*
 * Determine whether a node is a leaf node or not.
 */
static INLINE int bt_is_leaf(btree *bt, nodeptr n)
{
    return n[0].na.p == NULL;
}

/*
 * Create a new write-locked node, and return a pointer to it.
 */
static INLINE nodeptr bt_new_node(btree *bt, int nsubtrees)
{
    nodeptr ret = (nodecomponent *)smalloc(bt->propoffset + bt->propsize);
    ret[bt->maxdegree*2-1].i = nsubtrees;
    ret[bt->maxdegree*2+1].i = 1;      /* reference count 1 */
#ifdef TEST
    set_invalid_property(ret + bt->maxdegree * 2 + 2);
#else
    memset((char *)ret + bt->propoffset, 0, bt->propsize);
#endif
    testlock(TRUE, TRUE, ret);
    return ret;
}

/*
 * Destroy a node (must be write-locked).
 */
static INLINE void bt_destroy_node(btree *bt, nodeptr n)
{
    testlock(TRUE, FALSE, n);
    /* Free the property. */
    bt->propmerge(bt->userstate, NULL, NULL, n + bt->maxdegree * 2 + 2);
    sfree(n);
}

/*
 * Take an existing node and prepare to re-use it in a new context.
 */
static INLINE nodeptr bt_reuse_node(btree *bt, nodeptr n, int nsubtrees)
{
    testlock(TRUE, FALSE, n);
    testlock(TRUE, TRUE, n);
    n[bt->maxdegree*2-1].i = nsubtrees;
    return n;
}

/*
 * Return an extra reference to a node, for purposes of cloning. So
 * we have to update its reference count as well.
 */
static INLINE node_addr bt_ref_node(btree *bt, node_addr n)
{
    if (n.p)
	n.p[bt->maxdegree*2+1].i++;
    return n;
}

/*
 * Drop a node's reference count, for purposes of freeing. Returns
 * the new reference count. Typically this will be tested against
 * zero to see if the node needs to be physically freed; hence a
 * NULL node_addr causes a return of 1 (because this isn't
 * necessary).
 */
static INLINE int bt_unref_node(btree *bt, node_addr n)
{
    if (n.p) {
	n.p[bt->maxdegree*2+1].i--;
	return n.p[bt->maxdegree*2+1].i;
    } else
	return 1;		       /* a NULL node is considered OK */
}

/*
 * Clone a node during write unlocking, if its reference count is
 * more than one.
 */
static nodeptr bt_clone_node(btree *bt, nodeptr n)
{
    int i;
    nodeptr ret = (nodecomponent *)smalloc(bt->propoffset + bt->propsize);
    memcpy(ret, n, (bt->maxdegree*2+1) * sizeof(nodecomponent));
    if (bt->copy) {
	for (i = 0; i < bt_elements(bt, ret); i++) {
	    bt_element_t *e = bt_element(bt, ret, i);
	    bt_set_element(bt, ret, i, bt->copy(bt->userstate, e));
	}
    }
    ret[bt->maxdegree*2+1].i = 1;      /* clone has reference count 1 */
    n[bt->maxdegree*2+1].i--;	       /* drop original's ref count by one */
    /*
     * At this low level, we're allowed to reach directly into the
     * subtrees to fiddle with their reference counts without
     * having to lock them.
     */
    for (i = 0; i < bt_subtrees(bt, ret); i++) {
	node_addr na = bt_child(bt, ret, i);
	if (na.p)
	    na.p[bt->maxdegree*2+1].i++;   /* inc ref count of each child */
    }
    /*
     * Copy the user property explicitly (in case it contains a
     * pointer to an allocated area).
     */
    memset((char *)ret + bt->propoffset, 0, bt->propsize);
    bt->propmerge(bt->userstate, NULL, n + bt->maxdegree * 2 + 2,
		  ret + bt->maxdegree * 2 + 2);
    return ret;
}

/*
 * Return the node_addr for a currently locked node. NB that this
 * means node movement must take place during _locking_ rather than
 * unlocking!
 */
static INLINE node_addr bt_node_addr(btree *bt, nodeptr n)
{
    node_addr ret;
    ret.p = n;
    return ret;
}

/*
 * The bt_write_lock and bt_read_lock functions should gracefully
 * handle being asked to write-lock a null node pointer, and just
 * return a null nodeptr.
 */
static INLINE nodeptr bt_write_lock_child(btree *bt, nodeptr a, int index)
{
    node_addr addr = bt_child(bt, a, index);
    if (addr.p && addr.p[bt->maxdegree*2+1].i > 1) {
	nodeptr clone = bt_clone_node(bt, addr.p);
	bt_set_child(bt, a, index, bt_node_addr(bt, clone));
	testlock(TRUE, TRUE, clone);
	return clone;
    }
    testlock(TRUE, TRUE, addr.p);
    return addr.p;
}
static INLINE nodeptr bt_write_lock_root(btree *bt)
{
    node_addr addr = bt->root;
    if (addr.p && addr.p[bt->maxdegree*2+1].i > 1) {
	nodeptr clone = bt_clone_node(bt, addr.p);
	bt->root = bt_node_addr(bt, clone);
	testlock(TRUE, TRUE, clone);
	return clone;
    }
    testlock(TRUE, TRUE, addr.p);
    return addr.p;
}
static INLINE nodeptr bt_read_lock(btree *bt, node_addr a)
{
    testlock(FALSE, TRUE, a.p);
    return a.p;
}
#define bt_read_lock_root(bt) (bt_read_lock(bt, (bt)->root))
#define bt_read_lock_child(bt,a,index) (bt_read_lock(bt,bt_child(bt,a,index)))

static INLINE void bt_write_relock(btree *bt, nodeptr n, int props)
{
    int i, ns, count;

    /*
     * Update the count in the node.
     */
    ns = bt_subtrees(bt, n);
    count = ns-1;		       /* count the elements */
    for (i = 0; i < ns; i++)
	count += bt_child_count(bt, n, i);
    n[bt->maxdegree*2].i = count;
    testlock(TRUE, FALSE, n);
    testlock(TRUE, TRUE, n);

    /*
     * Update user read properties.
     */
    if (props && bt->propsize) {
	void *prevprop, *eltprop, *thisprop, *childprop;

	prevprop = NULL;
	eltprop = ialloc(bt->propsize);
	thisprop = (void *)((char *)n + bt->propoffset);

	for (i = 0; i < ns; i++) {
	    /* Merge a subtree's property into this one.
	     * Initially prevprop==NULL, meaning to just copy. */
	    if ( (childprop = bt_child_prop(bt, n, i)) != NULL ) {
		bt->propmerge(bt->userstate, prevprop, childprop, thisprop);
		prevprop = thisprop;
	    }

	    if (i < ns-1) {
		/* Now merge in the separating element. */
		bt->propmake(bt->userstate, bt_element(bt, n, i), eltprop);
		bt->propmerge(bt->userstate, prevprop, eltprop, thisprop);
		prevprop = thisprop;
	    }
	}

	ifree(eltprop);
    }
}

static INLINE node_addr bt_write_unlock_internal(btree *bt, nodeptr n,
						 int props)
{
    node_addr ret;

    bt_write_relock(bt, n, props);

    testlock(TRUE, FALSE, n);

    ret.p = n;
    return ret;
}

static INLINE node_addr bt_write_unlock(btree *bt, nodeptr n)
{
    return bt_write_unlock_internal(bt, n, TRUE);
}

static INLINE void bt_read_unlock(btree *bt, nodeptr n)
{
    /*
     * For trees in memory, we do nothing here, except run some
     * optional testing.
     */
    testlock(FALSE, FALSE, n);
}

/* ----------------------------------------------------------------------
 * Higher-level helper functions, which should be independent of
 * the knowledge of precise node structure in the above code.
 */

/*
 * Return the count of items below a node that appear before the
 * start of a given subtree.
 */
static int bt_child_startpos(btree *bt, nodeptr n, int index)
{
    int pos = 0;

    while (index > 0) {
	index--;
	pos += bt_child_count(bt, n, index) + 1;   /* 1 for separating elt */
    }
    return pos;
}

/*
 * Create a new root node for a tree.
 */
static void bt_new_root(btree *bt, node_addr left, node_addr right,
			bt_element_t element)
{
    nodeptr n;
    n = bt_new_node(bt, 2);
    bt_set_child(bt, n, 0, left);
    bt_set_child(bt, n, 1, right);
    bt_set_element(bt, n, 0, element);
    bt->root = bt_write_unlock(bt, n);
    bt->depth++;
}

/*
 * Discard the root node of a tree, and enshrine a new node as the
 * root. Expects to be passed a write-locked nodeptr to the old
 * root.
 */
static void bt_shift_root(btree *bt, nodeptr n, node_addr na)
{
    bt_destroy_node(bt, n);
    bt->root = na;
    bt->depth--;
}

/*
 * Given a numeric index within a node, find which subtree we would
 * descend to in order to find that index.
 * 
 * Updates `pos' to give the numeric index within the subtree
 * found. Also returns `ends' (if non-NULL), which has bit 0 set if
 * the index is at the very left edge of the subtree, and/or bit 1
 * if it's at the very right edge.
 * 
 * Return value is the number of the subtree (0 upwards).
 */
#define ENDS_NONE  0
#define ENDS_LEFT  1
#define ENDS_RIGHT 2
#define ENDS_BOTH  3
static int bt_lookup_pos(btree *bt, nodeptr n, int *pos, int *ends)
{
    int child = 0;
    int nchildren = bt_subtrees(bt, n);

    while (child < nchildren) {
	int count = bt_child_count(bt, n, child);
	if (*pos <= count) {
	    if (ends) {
		*ends = 0;
		if (*pos == count)
		    *ends |= ENDS_RIGHT;
		if (*pos == 0)
		    *ends |= ENDS_LEFT;
	    }
	    return child;
	}
	*pos -= count + 1;	       /* 1 for the separating element */
	child++;
    }
    return -1;			       /* ran off the end; shouldn't happen */
}

/*
 * Given an element to search for within a node, find either the
 * element, or which subtree we would descend to to continue
 * searching for that element.
 * 
 * Return value is either the index of the element, or the index of
 * the subtree (both 0 upwards). `is_elt' returns FALSE or TRUE
 * respectively.
 * 
 * Since this may be used by bt_find() with an alternative cmpfn_t,
 * we always pass the input element as the first argument to cmp.
 */
static int bt_lookup_cmp(btree *bt, nodeptr n, bt_element_t element,
			 cmpfn_t cmp, int *is_elt)
{
    int mintree = 0, maxtree = bt_subtrees(bt, n)-1;

    while (mintree < maxtree) {
	int elt = (maxtree + mintree) / 2;
	int c = cmp(bt->userstate, element, bt_element(bt, n, elt));

	if (c == 0) {
	    *is_elt = TRUE;
	    return elt;
	} else if (c < 0) {
	    /*
	     * `element' is less than element `elt'. So it can be
	     * in subtree number `elt' at the highest.
	     */
	    maxtree = elt;
	} else { /* c > 0 */
	    /*
	     * `element' is greater than element `elt'. So it can
	     * be in subtree number (elt+1) at the lowest.
	     */
	    mintree = elt+1;
	}
    }

    /*
     * If we reach here without returning, we must have narrowed
     * our search to the point where mintree = maxtree. So the
     * element is not in the node itself and we know which subtree
     * to search next.
     */
    assert(mintree == maxtree);
    *is_elt = FALSE;
    return mintree;
}

/*
 * Generic transformations on B-tree nodes.
 * 
 * This function divides essentially into an input side and an
 * output side. The input side accumulates a list of items
 * node,element,node,element,...,element,node; the output side
 * writes those items into either one or two nodes.
 * 
 * `intype' can be:
 *
 *  - NODE_AS_IS. The input list is the contents of in1, followed
 *    by inelt, followed by the contents of in2. The `extra'
 *    parameters are unused, as is `inaux'.
 * 
 *  - NODE_ADD_ELT. `in2' is unused. The input list is the contents
 *    of `in1', but with subtree pointer number `inaux' replaced by
 *    extra1/inelt/extra2.
 * 
 *  - NODE_DEL_ELT. `in2' and `inelt' are unused, as is `extra2'.
 *    The input list is the contents of `in1', but with element
 *    pointer number `inaux' and its surrounding two subtrees
 *    replaced by extra1.
 * 
 * Having obtained the input list, it is then written to one or two
 * output nodes. If `splitpos' is NODE_JOIN, everything is written
 * into one output node `out1'. Otherwise, `splitpos' is treated as
 * an element index within the input list; that element is returned
 * in `outelt', and the contents of the list is divided there and
 * returned in nodes `out1' and `out2'.
 * 
 * This function will re-use nodes in the `obvious' order. If two
 * nodes are passed in and two nodes are output, they'll be the
 * same nodes; if one node is passed in and one node output, it
 * will be the same node too. If two are passed in and only one
 * output, the first one will be used and the second destroyed; if
 * one node is passed in and two are output, the one passed in will
 * be the first of those returned, and the second will be new.
 */
#define NODE_AS_IS 1
#define NODE_ADD_ELT 2
#define NODE_DEL_ELT 3
#define NODE_JOIN -1
static void bt_xform(btree *bt, int intype, int inaux,
		     nodeptr in1, nodeptr in2, bt_element_t inelt,
		     node_addr extra1, node_addr extra2,
		     int splitpos, nodeptr *out1, nodeptr *out2,
		     bt_element_t *outelt)
{
    node_addr *nodes;
    bt_element_t *elements;
    nodeptr ret1, ret2;
    int n1, n2, off2, i, j;

    nodes = inewn(node_addr, 2 * bt_max_subtrees(bt));
    elements = inewn(bt_element_t, 2 * bt_max_subtrees(bt));

    /*
     * Accumulate the input list.
     */
    switch(intype) {
      case NODE_AS_IS:
	n1 = bt_subtrees(bt, in1);
	n2 = bt_subtrees(bt, in2);
	off2 = 0;
	break;
      case NODE_ADD_ELT:
	in2 = in1;
	n1 = inaux+1;
	n2 = bt_subtrees(bt, in1) - inaux;
	off2 = inaux;
	break;
      case NODE_DEL_ELT:
	in2 = in1;
	n1 = inaux+1;
	n2 = bt_subtrees(bt, in1) - inaux - 1;
	off2 = inaux+1;
	break;
    }
    i = j = 0;
    while (j < n1) {
	nodes[i] = bt_child(bt, in1, j);
	if (j+1 < n1)
	    elements[i] = bt_element(bt, in1, j);
	i++, j++;
    }
    if (intype == NODE_DEL_ELT) {
	i--;
    }
    j = 0;
    while (j < n2) {
	nodes[i] = bt_child(bt, in2, off2+j);
	if (j+1 < n2)
	    elements[i] = bt_element(bt, in2, off2+j);
	i++, j++;
    }
    switch (intype) {
      case NODE_AS_IS:
	elements[n1-1] = inelt;
	break;
      case NODE_ADD_ELT:
	nodes[n1-1] = extra1;
	nodes[n1] = extra2;
	elements[n1-1] = inelt;
	break;
      case NODE_DEL_ELT:
	nodes[n1-1] = extra1;
	break;
    }

    /*
     * Now determine how many subtrees go in each output node, and
     * actually create the nodes to be returned.
     */
    if (splitpos != NODE_JOIN) {
	n1 = splitpos+1, n2 = i - splitpos - 1;
	if (outelt)
	    *outelt = elements[splitpos];
    } else {
	n1 = i, n2 = 0;
    }

    ret1 = bt_reuse_node(bt, in1, n1);
    if (intype == NODE_AS_IS && in2) {
	/* We have a second input node. */
	if (n2)
	    ret2 = bt_reuse_node(bt, in2, n2);
	else
	    bt_destroy_node(bt, in2);
    } else {
	/* We have no second input node. */
	if (n2)
	    ret2 = bt_new_node(bt, n2);
	else
	    ret2 = NULL;
    }

    if (out1) *out1 = ret1;
    if (out2) *out2 = ret2;

    for (i = 0; i < n1; i++) {
	bt_set_child(bt, ret1, i, nodes[i]);
	if (i+1 < n1)
	    bt_set_element(bt, ret1, i, elements[i]);
    }
    if (n2) {
	if (outelt) *outelt = elements[n1-1];
	for (i = 0; i < n2; i++) {
	    bt_set_child(bt, ret2, i, nodes[n1+i]);
	    if (i+1 < n2)
		bt_set_element(bt, ret2, i, elements[n1+i]);
	}
    }

    ifree(nodes);
    ifree(elements);
}

/*
 * Fiddly little compare functions for use in special cases of
 * findrelpos. One always returns +1 (a > b), the other always
 * returns -1 (a < b).
 */
static int bt_cmp_greater(void *state,
			  const bt_element_t a, const bt_element_t b)
{
    return +1;
}
static int bt_cmp_less(void *state,
		       const bt_element_t a, const bt_element_t b)
{
    return -1;
}

/* ----------------------------------------------------------------------
 * User-visible administration routines.
 */

btree *bt_new(cmpfn_t cmp, copyfn_t copy, freefn_t freeelt,
	      int propsize, int propalign, propmakefn_t propmake,
	      propmergefn_t propmerge, void *state, int mindegree)
{
    btree *ret;

    ret = new1(btree);
    ret->mindegree = mindegree;
    ret->maxdegree = 2*mindegree;
    ret->depth = 0;		       /* not even a root right now */
    ret->root = NODE_ADDR_NULL;
    ret->cmp = cmp;
    ret->copy = copy;
    ret->freeelt = freeelt;
    ret->propsize = propsize;
    ret->propalign = propalign;
    ret->propoffset = sizeof(nodecomponent) * (ret->maxdegree*2 + 2);
    if (propalign > 0) {
	ret->propoffset += propalign - 1;
	ret->propoffset -= ret->propoffset % propalign;
    }
    ret->propmake = propmake;
    ret->propmerge = propmerge;
    ret->userstate = state;

    return ret;
}

static void bt_free_node(btree *bt, nodeptr n)
{
    int i;

    for (i = 0; i < bt_subtrees(bt, n); i++) {
	node_addr na;
	nodeptr n2;

	na = bt_child(bt, n, i);
	if (!bt_unref_node(bt, na)) {
	    n2 = bt_write_lock_child(bt, n, i);
	    bt_free_node(bt, n2);
	}
    }

    if (bt->freeelt) {
	for (i = 0; i < bt_subtrees(bt, n)-1; i++)
	    bt->freeelt(bt->userstate, bt_element(bt, n, i));
    }

    bt_destroy_node(bt, n);
}

void bt_free(btree *bt)
{
    nodeptr n;

    if (!bt_unref_node(bt, bt->root)) {
	n = bt_write_lock_root(bt);
	bt_free_node(bt, n);
    }

    sfree(bt);
}

btree *bt_clone(btree *bt)
{
    btree *bt2;

    bt2 = bt_new(bt->cmp, bt->copy, bt->freeelt, bt->propsize, bt->propalign,
		 bt->propmake, bt->propmerge, bt->userstate, bt->mindegree);
    bt2->depth = bt->depth;
    bt2->root = bt_ref_node(bt, bt->root);
    return bt2;
}

/*
 * Nice simple function to count the size of a tree.
 */
int bt_count(btree *bt)
{
    int count;
    nodeptr n;

    n = bt_read_lock_root(bt);
    if (n) {
	count = bt_node_count(bt, n);
	bt_read_unlock(bt, n);
	return count;
    } else {
	return 0;
    }
}

/* ----------------------------------------------------------------------
 * Actual B-tree algorithms.
 */

/*
 * Find an element by numeric index. bt_index_w is the same, but
 * works with write locks instead of read locks, so it guarantees
 * to return an element with only one reference to it. (You'd use
 * this if you were using tree cloning, and wanted to modify the
 * element once you'd found it.)
 */
bt_element_t bt_index(btree *bt, int index)
{
    nodeptr n, n2;
    int child, ends;

    n = bt_read_lock_root(bt);

    if (index < 0 || index >= bt_node_count(bt, n)) {
	bt_read_unlock(bt, n);
	return NULL;
    }

    while (1) {
	child = bt_lookup_pos(bt, n, &index, &ends);
	if (ends & ENDS_RIGHT) {
	    bt_element_t ret = bt_element(bt, n, child);
	    bt_read_unlock(bt, n);
	    return ret;
	}
	n2 = bt_read_lock_child(bt, n, child);
	bt_read_unlock(bt, n);
	n = n2;
	assert(n != NULL);
    }
}

bt_element_t bt_index_w(btree *bt, int index)
{
    nodeptr n, n2;
    int nnodes, child, ends;
    nodeptr *nodes;
    bt_element_t ret;

    nodes = inewn(nodeptr, bt->depth+1);
    nnodes = 0;

    n = bt_write_lock_root(bt);

    if (index < 0 || index >= bt_node_count(bt, n)) {
	bt_write_unlock(bt, n);
	return NULL;
    }

    while (1) {
	nodes[nnodes++] = n;
	child = bt_lookup_pos(bt, n, &index, &ends);
	if (ends & ENDS_RIGHT) {
	    ret = bt_element(bt, n, child);
	    break;
	}
	n2 = bt_write_lock_child(bt, n, child);
	n = n2;
	assert(n != NULL);
    }

    while (nnodes-- > 0)
	bt_write_unlock(bt, nodes[nnodes]);

    return ret;
}

/*
 * Search for an element by sorted order.
 */
bt_element_t bt_findrelpos(btree *bt, bt_element_t element, cmpfn_t cmp,
			   int relation, int *index)
{
    nodeptr n, n2;
    int child, is_elt;
    bt_element_t gotit;
    int pos = 0;

    if (!cmp) cmp = bt->cmp;

    /*
     * Special case: relation LT/GT and element NULL means get an
     * extreme element of the tree. We do this by fudging the
     * compare function so that our NULL element will be considered
     * infinitely large or infinitely small.
     */
    if (element == NULL) {
	assert(relation == BT_REL_LT || relation == BT_REL_GT);
	if (relation == BT_REL_LT)
	    cmp = bt_cmp_greater;      /* always returns a > b */
	else
	    cmp = bt_cmp_less;	       /* always returns a < b */
    }

    gotit = NULL;
    n = bt_read_lock_root(bt);
    if (!n)
	return NULL;
    while (n) {
	child = bt_lookup_cmp(bt, n, element, cmp, &is_elt);
	if (is_elt) {
	    pos += bt_child_startpos(bt, n, child+1) - 1;
	    gotit = bt_element(bt, n, child);
	    bt_read_unlock(bt, n);
	    break;
	} else {
	    pos += bt_child_startpos(bt, n, child);
	    n2 = bt_read_lock_child(bt, n, child);
	    bt_read_unlock(bt, n);
	    n = n2;
	}
    }

    /*
     * Now all nodes are unlocked, and we are _either_ (a) holding
     * an element in `gotit' whose index we have in `pos', _or_ (b)
     * holding nothing in `gotit' but we know the index of the
     * next-higher element.
     */
    if (gotit) {
	/*
	 * We have the real element. For EQ, LE and GE relations we
	 * can now just return it; otherwise we must return the
	 * next element down or up.
	 */
	if (relation == BT_REL_LT)
	    gotit = bt_index(bt, --pos);
	else if (relation == BT_REL_GT)
	    gotit = bt_index(bt, ++pos);
    } else {
	/*
	 * We don't have the real element. For EQ relation we now
	 * just give up; for everything else we return the next
	 * element down or up.
	 */
	if (relation == BT_REL_LT || relation == BT_REL_LE)
	    gotit = bt_index(bt, --pos);
	else if (relation == BT_REL_GT || relation == BT_REL_GE)
	    gotit = bt_index(bt, pos);
    }

    if (gotit && index) *index = pos;
    return gotit;
}
bt_element_t bt_findrel(btree *bt, bt_element_t element, cmpfn_t cmp,
			int relation)
{
    return bt_findrelpos(bt, element, cmp, relation, NULL);
}
bt_element_t bt_findpos(btree *bt, bt_element_t element, cmpfn_t cmp,
			int *index)
{
    return bt_findrelpos(bt, element, cmp, BT_REL_EQ, index);
}
bt_element_t bt_find(btree *bt, bt_element_t element, cmpfn_t cmp)
{
    return bt_findrelpos(bt, element, cmp, BT_REL_EQ, NULL);
}

/*
 * Find an element by property-based search. Returns the element
 * (if one is selected - the search can also terminate by
 * descending to a nonexistent subtree of a leaf node, equivalent
 * to selecting the _gap_ between two elements); also returns the
 * index of either the element or the gap in `*index' if `index' is
 * non-NULL.
 */
bt_element_t bt_propfind(btree *bt, searchfn_t search, void *sstate,
			 int *index)
{
    nodeptr n, n2;
    int i, j, count, is_elt;
    void **props;
    int *counts;
    bt_element_t *elts;
    bt_element_t *e = NULL;

    props = inewn(void *, bt->maxdegree);
    counts = inewn(int, bt->maxdegree);
    elts = inewn(bt_element_t, bt->maxdegree);

    n = bt_read_lock_root(bt);

    count = 0;

    while (n) {
	int ntrees = bt_subtrees(bt, n);

	/*
	 * Prepare the arguments to the search function.
	 */
	for (i = 0; i < ntrees; i++) {
	    props[i] = bt_child_prop(bt, n, i);
	    counts[i] = bt_child_count(bt, n, i);
	    if (i < ntrees-1)
		elts[i] = bt_element(bt, n, i);
	}

	/*
	 * Call the search function.
	 */
	i = search(bt->userstate, sstate, ntrees,
		   props, counts, elts, &is_elt);

	if (!is_elt) {
	    /*
	     * Descend to subtree i. Update `count' to consider
	     * everything (both subtrees and elements) before that
	     * subtree.
	     */
	    for (j = 0; j < i; j++)
		count += 1 + bt_child_count(bt, n, j);
	    n2 = bt_read_lock_child(bt, n, i);
	    bt_read_unlock(bt, n);
	    n = n2;
	} else {
	    /*
	     * Return element i. Update `count' to consider
	     * everything (both subtrees and elements) before that
	     * element.
	     */
	    for (j = 0; j <= i; j++)
		count += 1 + bt_child_count(bt, n, j);
	    count--;		       /* don't count element i itself */
	    e = bt_element(bt, n, i);
	    bt_read_unlock(bt, n);
	    break;
	}
    }

    ifree(props);
    ifree(counts);
    ifree(elts);

    if (index) *index = count;
    return e;
}

/*
 * Replace the element at a numeric index by a new element. Returns
 * the old element.
 * 
 * Can also be used when the new element is the _same_ as the old
 * element, but has changed in some way that will affect user
 * properties.
 */
bt_element_t bt_replace(btree *bt, bt_element_t element, int index)
{
    nodeptr n;
    nodeptr *nodes;
    bt_element_t ret;
    int nnodes, child, ends;

    nodes = inewn(nodeptr, bt->depth+1);
    nnodes = 0;

    n = bt_write_lock_root(bt);

    if (index < 0 || index >= bt_node_count(bt, n)) {
	bt_write_unlock(bt, n);
	return NULL;
    }

    while (1) {
	nodes[nnodes++] = n;
	child = bt_lookup_pos(bt, n, &index, &ends);
	if (ends & ENDS_RIGHT) {
	    ret = bt_element(bt, n, child);
	    bt_set_element(bt, n, child, element);
	    break;
	}
	n = bt_write_lock_child(bt, n, child);
	assert(n != NULL);
    }
    
    while (nnodes-- > 0)
	bt_write_unlock(bt, nodes[nnodes]);

    return ret;
}

/*
 * Add at a specific position. As we search down the tree we must
 * write-lock every node we meet, since otherwise we might fail to
 * clone nodes that will end up pointing to different things.
 */
void bt_addpos(btree *bt, bt_element_t element, int pos)
{
    nodeptr n;
    node_addr left, right, single;
    nodeptr *nodes;
    int *childposns;
    int nnodes, child;

    /*
     * Since in a reference-counted tree we can't have parent
     * links, we will have to use O(depth) space to store the list
     * of nodeptrs we have gone through, so we can un-write-lock
     * them when we've finished. We also store the subtree index we
     * descended to at each stage.
     */
    nodes = inewn(nodeptr, bt->depth+1);
    childposns = inewn(int, bt->depth+1);
    nnodes = 0;

    n = bt_write_lock_root(bt);

    assert(pos >= 0 && pos <= (n ? bt_node_count(bt, n) : 0));

    /*
     * Scan down the tree, write-locking nodes, until we find the
     * empty subtree where we want to insert the item.
     */
    while (n) {
	nodes[nnodes] = n;
	child = bt_lookup_pos(bt, n, &pos, NULL);
	childposns[nnodes] = child;
	nnodes++;
	n = bt_write_lock_child(bt, n, child);
    }

    left = right = NODE_ADDR_NULL;

    /*
     * Now nodes[nnodes-1] wants to have subtree index
     * childposns[nnodes-1] replaced by the node/element/node triple
     * (left,element,right). Propagate this up the tree until we
     * can stop.
     */
    while (nnodes-- > 0) {
	n = nodes[nnodes];
	if (bt_subtrees(bt, n) == bt_max_subtrees(bt)) {
	    nodeptr lptr, rptr;
	    /* Split the node and carry on up. */
	    bt_xform(bt, NODE_ADD_ELT, childposns[nnodes],
		     n, NULL, element, left, right,
		     bt_min_subtrees(bt), &lptr, &rptr, &element);
	    left = bt_write_unlock(bt, lptr);
	    right = bt_write_unlock(bt, rptr);
	} else {
	    bt_xform(bt, NODE_ADD_ELT, childposns[nnodes],
		     n, NULL, element, left, right,
		     NODE_JOIN, &n, NULL, NULL);
	    single = bt_write_unlock(bt, n);
	    break;
	}
    }

    /*
     * If nnodes < 0, we have just split the root and we need to
     * build a new root node.
     */
    if (nnodes < 0) {
	bt_new_root(bt, left, right, element);
    } else {
	/*
	 * Now nodes[nnodes-1] just wants to have child pointer
	 * child[nnodes-1] replaced by `single', in case the
	 * subtree was moved. Propagate this back up to the root,
	 * unlocking all nodes.
	 */
	while (nnodes-- > 0) {
	    bt_set_child(bt, nodes[nnodes], childposns[nnodes], single);
	    single = bt_write_unlock(bt, nodes[nnodes]);
	}
    }

    ifree(nodes);
    ifree(childposns);
}

/*
 * Add an element in sorted order. This is a wrapper on bt_addpos()
 * which finds the numeric index to add the item at and then calls
 * addpos. This isn't an optimal use of time, but it saves space by
 * avoiding starting to clone multiply-linked nodes until it's
 * known that the item _can_ be added to the tree (and isn't
 * duplicated in it already).
 */
bt_element_t bt_add(btree *bt, bt_element_t element)
{
    nodeptr n, n2;
    int child, is_elt;
    int pos = 0;

    n = bt_read_lock_root(bt);
    while (n) {
	child = bt_lookup_cmp(bt, n, element, bt->cmp, &is_elt);
	if (is_elt) {
	    bt_read_unlock(bt, n);
	    return bt_element(bt, n, child);   /* element exists already */
	} else {
	    pos += bt_child_startpos(bt, n, child);
	    n2 = bt_read_lock_child(bt, n, child);
	    bt_read_unlock(bt, n);
	    n = n2;
	}
    }
    bt_addpos(bt, element, pos);
    return element;
}

/*
 * Delete an element given its numeric position. Returns the
 * element deleted.
 */
bt_element_t bt_delpos(btree *bt, int pos)
{
    nodeptr n, c, c2, saved_n;
    nodeptr *nodes;
    int nnodes, child, nroot, pos2, ends, st, splitpoint, saved_pos;
    bt_element_t e, ret;

    /*
     * Just like in bt_add, we store the set of nodeptrs we
     * write-locked on the way down, so we can unlock them on the
     * way back up.
     */
    nodes = inewn(nodeptr, bt->depth+1);
    nnodes = 0;

    n = bt_write_lock_root(bt);
    nroot = TRUE;
    saved_n = NULL;

    if (!n || pos < 0 || pos >= bt_node_count(bt, n)) {
	if (n)
	    bt_write_unlock(bt, n);
	return NULL;
    }

    while (1) {
	nodes[nnodes++] = n;

	/*
	 * Find out which subtree to descend to.
	 */
	pos2 = pos;
	child = bt_lookup_pos(bt, n, &pos, &ends);
	c = bt_write_lock_child(bt, n, child);
	if (c && bt_subtrees(bt, c) == bt_min_subtrees(bt)) {
	    /*
	     * We're trying to descend to a subtree that's of
	     * minimum size. Do something!
	     */
	    if (child > 0) {
		/*
		 * Either move a subtree from the left sibling, or
		 * merge with it. (Traditionally we would only
		 * merge if we can't move a subtree from _either_
		 * sibling, but this way avoids too many extra
		 * write locks.)
		 */
		c2 = c;
		c = bt_write_lock_child(bt, n, child-1);
		e = bt_element(bt, n, child-1);
		st = bt_subtrees(bt, c);
		if (st > bt_min_subtrees(bt))
		    splitpoint = st - 2;
		else
		    splitpoint = NODE_JOIN;
		child--;
	    } else {
		/*
		 * Likewise on the right-hand side.
		 */
		c2 = bt_write_lock_child(bt, n, child+1);
		e = bt_element(bt, n, child);
		st = bt_subtrees(bt, c2);
		if (st > bt_min_subtrees(bt))
		    splitpoint = bt_min_subtrees(bt);
		else
		    splitpoint = NODE_JOIN;
	    }
	    
	    if (splitpoint == NODE_JOIN) {
		/*
		 * So if we're merging nodes, go to it...
		 */
		bt_xform(bt, NODE_AS_IS, 0,
			 c, c2, e, NODE_ADDR_NULL, NODE_ADDR_NULL,
			 NODE_JOIN, &c, NULL, NULL);
		bt_xform(bt, NODE_DEL_ELT, child,
			 n, NULL, NULL, bt_node_addr(bt, c), NODE_ADDR_NULL,
			 NODE_JOIN, &n, NULL, NULL);
		if (nroot && bt_subtrees(bt, n) == 1) {
		    /*
		     * Whoops, we just merged the last two children
		     * of the root. Better relocate the root.
		     */
		    bt_shift_root(bt, n, bt_node_addr(bt, c));
		    nnodes--;	       /* don't leave it in nodes[]! */
		    n = NULL;
		    bt_write_relock(bt, c, TRUE);
		} else
		    bt_write_unlock(bt, c);
	    } else {
		/*
		 * Or if we're redistributing subtrees, go to that.
		 */
		bt_xform(bt, NODE_AS_IS, 0,
			 c, c2, e, NODE_ADDR_NULL, NODE_ADDR_NULL,
			 splitpoint, &c, &c2, &e);
		bt_set_element(bt, n, child, e);
		bt_write_unlock(bt, c);
		bt_write_unlock(bt, c2);
	    }

	    if (n) {
		/* Recompute the counts in n so we can do lookups again. */
		bt_write_relock(bt, n, TRUE);

		/* Having done the transform, redo the position lookup. */
		pos = pos2;
		child = bt_lookup_pos(bt, n, &pos, &ends);
		c = bt_write_lock_child(bt, n, child);
	    } else {
		pos = pos2;
	    }
	}

	/*
	 * Now see if this node contains the element we're
	 * looking for.
	 */
	if (n && (ends & ENDS_RIGHT)) {
	    /*
	     * It does. Element number `child' is the element we
	     * want to delete. See if this is a leaf node...
	     */
	    if (!bt_is_leaf(bt, n)) {
		/*
		 * It's not a leaf node. So we save the nodeptr and
		 * element index for later reference, and decrement
		 * `pos' so that we're searching for the element to its
		 * left, which _will_ be in a leaf node.
		 */
		saved_n = n;
		saved_pos = child;
		pos--;
	    } else {
		/*
		 * We've reached a leaf node. Check to see if an
		 * internal-node position was stored in saved_n and
		 * saved_pos, and move this element there if so.
		 */
		if (saved_n) {
		    ret = bt_element(bt, saved_n, saved_pos);
		    bt_set_element(bt, saved_n, saved_pos,
				   bt_element(bt, n, child));
		} else {
		    ret = bt_element(bt, n, child);
		}
		/* Then delete it from the leaf node. */
		bt_xform(bt, NODE_DEL_ELT, child,
			 n, NULL, NULL, NODE_ADDR_NULL, NODE_ADDR_NULL,
			 NODE_JOIN, &n, NULL, NULL);
		/*
		 * Final special case: if this is the root node and
		 * we've just deleted its last element, we should
		 * destroy it and leave a completely empty tree.
		 */
		if (nroot && bt_subtrees(bt, n) == 1) {
		    bt_shift_root(bt, n, NODE_ADDR_NULL);
		    nnodes--;	       /* and take it out of nodes[] */
		}
		/* Now we're done */
		break;
	    }
	}

	/* Descend to the child and go round again. */
	n = c;
	nroot = FALSE;
    }

    /*
     * All done. Zip back up the tree un-write-locking nodes.
     */
    while (nnodes-- > 0)
	bt_write_unlock(bt, nodes[nnodes]);

    ifree(nodes);

    return ret;
}

/*
 * Delete an element in sorted order.
 */
bt_element_t bt_del(btree *bt, bt_element_t element)
{
    int index;
    if (!bt_findrelpos(bt, element, NULL, BT_REL_EQ, &index))
	return NULL;		       /* wasn't there */
    return bt_delpos(bt, index);
}

/*
 * Join two trees together, given their respective depths and a
 * middle element. Puts the resulting tree in the root of `bt'.
 * 
 * This internal routine assumes that the trees have the same
 * degree.
 * 
 * The input nodeptrs are assumed to be write-locked, but none of
 * their children are yet write-locked.
 */
static void bt_join_internal(btree *bt, nodeptr lp, nodeptr rp,
			    bt_element_t sep, int ld, int rd)
{
    nodeptr *nodes;
    int *childposns;
    int nnodes, nodessize;
    int lsub, rsub;

    /*
     * We will need to store parent nodes up to the difference
     * between ld and rd.
     */
    nodessize = (ld < rd ? rd-ld : ld-rd);
    if (nodessize) {		       /* we may not need _any_! */
	nodes = inewn(nodeptr, nodessize);
	childposns = inewn(int, nodessize);
    }
    nnodes = 0;

    if (ld > rd) {
	bt->root = bt_node_addr(bt, lp);
	bt->depth = ld;
	/* If the left tree is taller, search down its right-hand edge. */
	while (ld > rd) {
	    int child = bt_subtrees(bt, lp) - 1;
	    nodeptr n = bt_write_lock_child(bt, lp, child);
	    nodes[nnodes] = lp;
	    childposns[nnodes] = child;
	    nnodes++;
	    lp = n;
	    ld--;
	}
    } else {
	bt->root = bt_node_addr(bt, rp);
	bt->depth = rd;
	/* If the right tree is taller, search down its left-hand edge. */
	while (rd > ld) {
	    nodeptr n = bt_write_lock_child(bt, rp, 0);
	    nodes[nnodes] = rp;
	    childposns[nnodes] = 0;
	    nnodes++;
	    rp = n;
	    rd--;
	}
    }

    /*
     * So we now want to combine nodes lp and rp into either one or
     * two plausibly-sized nodes, whichever is feasible. We have a
     * joining element `sep'.
     */
    lsub = (lp ? bt_subtrees(bt, lp) : 0);
    rsub = (rp ? bt_subtrees(bt, rp) : 0);
    if (lp && rp && lsub + rsub <= bt_max_subtrees(bt)) {
	node_addr la;
	/* Join the nodes into one. */
	bt_xform(bt, NODE_AS_IS, 0, lp, rp, sep,
		 NODE_ADDR_NULL, NODE_ADDR_NULL,
		 NODE_JOIN, &lp, NULL, NULL);
	/* Unlock the node. */
	la = bt_write_unlock(bt, lp);
	/* Update the child pointer in the next node up. */
	if (nnodes > 0)
	    bt_set_child(bt, nodes[nnodes-1], childposns[nnodes-1], la);
	else
	    bt->root = la;
    } else {
	node_addr la, ra;
	if (!lp || !rp) {
	    la = NODE_ADDR_NULL;
	    ra = NODE_ADDR_NULL;
	} else {
	    int lsize, rsize;
	    /* Re-split the nodes into two plausibly sized ones. */
	    lsize = lsub + rsub;
	    rsize = lsize / 2;
	    lsize -= rsize;
	    bt_xform(bt, NODE_AS_IS, 0, lp, rp, sep,
		     NODE_ADDR_NULL, NODE_ADDR_NULL,
		     lsize-1, &lp, &rp, &sep);
	    /* Unlock the nodes. */
	    la = bt_write_unlock(bt, lp);
	    ra = bt_write_unlock(bt, rp);
	}

	/*
	 * Now we have to do the addition thing: progress up the
	 * tree replacing a single subtree pointer with the
	 * la/sep/ra assembly, until no more nodes have to split as
	 * a result.
	 */
	while (nnodes-- > 0) {
	    nodeptr n = nodes[nnodes];
	    if (bt_subtrees(bt, n) == bt_max_subtrees(bt)) {
		/* Split the node and carry on up. */
		bt_xform(bt, NODE_ADD_ELT, childposns[nnodes],
			 n, NULL, sep, la, ra,
			 bt_min_subtrees(bt), &lp, &rp, &sep);
		la = bt_write_unlock(bt, lp);
		ra = bt_write_unlock(bt, rp);
	    } else {
		bt_xform(bt, NODE_ADD_ELT, childposns[nnodes],
			 n, NULL, sep, la, ra,
			 NODE_JOIN, &n, NULL, NULL);
		bt_write_unlock(bt, n);
		break;
	    }
	}

	/*
	 * If nnodes < 0, we have just split the root and we need
	 * to build a new root node.
	 */
	if (nnodes < 0)
	    bt_new_root(bt, la, ra, sep);
    }

    /*
     * Now we just need to go back up and unlock any remaining
     * nodes. Also here we ensure the root points where it should.
     */
    while (nnodes-- > 0) {
	node_addr na;
	na = bt_write_unlock(bt, nodes[nnodes]);
	if (nnodes == 0)
	    bt->root = na;
    }

    if (nodessize) {
	ifree(nodes);
	ifree(childposns);
    }
}

/*
 * External interfaces to the join functionality: join and joinr
 * (differing only in which B-tree structure they leave without any
 * elements, and which they return the combined tree in).
 */
btree *bt_join(btree *bt1, btree *bt2)
{
    nodeptr root1, root2;
    int size2;

    size2 = bt_count(bt2);
    if (size2 > 0) {
	bt_element_t sep;

	if (bt1->cmp) {
	    /*
	     * The trees are ordered, so verify the ordering
	     * condition: ensure nothing in bt1 is greater than or
	     * equal to the minimum element in bt2.
	     */
	    sep = bt_index(bt2, 0);
	    sep = bt_findrelpos(bt1, sep, NULL, BT_REL_GE, NULL);
	    if (sep)
		return NULL;
	}

	sep = bt_delpos(bt2, 0);
	root1 = bt_write_lock_root(bt1);
	root2 = bt_write_lock_root(bt2);
	bt_join_internal(bt1, root1, root2, sep, bt1->depth, bt2->depth);
	bt2->root = NODE_ADDR_NULL;
	bt2->depth = 0;
    }
    return bt1;
}

btree *bt_joinr(btree *bt1, btree *bt2)
{
    nodeptr root1, root2;
    int size1;
    
    size1 = bt_count(bt1);
    if (size1 > 0) {
	bt_element_t sep;

	if (bt2->cmp) {
	    /*
	     * The trees are ordered, so verify the ordering
	     * condition: ensure nothing in bt2 is less than or
	     * equal to the maximum element in bt1.
	     */
	    sep = bt_index(bt1, size1-1);
	    sep = bt_findrelpos(bt2, sep, NULL, BT_REL_LE, NULL);
	    if (sep)
		return NULL;
	}

	sep = bt_delpos(bt1, size1-1);
	root1 = bt_write_lock_root(bt1);
	root2 = bt_write_lock_root(bt2);
	bt_join_internal(bt2, root1, root2, sep, bt1->depth, bt2->depth);
	bt1->root = NODE_ADDR_NULL;
	bt1->depth = 0;
    }
    return bt2;
}

/*
 * Perform the healing process after a tree has been split. `rhs'
 * is set if the cut edge is the one on the right.
 */
static void bt_split_heal(btree *bt, int rhs)
{
    nodeptr n;
    nodeptr *nodes;
    int nnodes;

    nodes = inewn(nodeptr, bt->depth);
    nnodes = 0;

    n = bt_write_lock_root(bt);

    /*
     * First dispense with completely trivial cases: a root node
     * containing only one subtree can be thrown away instantly.
     */
    while (n && bt_subtrees(bt, n) == 1) {
	nodeptr n2 = bt_write_lock_child(bt, n, 0);
	bt_shift_root(bt, n, bt_node_addr(bt, n2));
	n = n2;
    }

    /*
     * Now we have a plausible root node. Start going down the cut
     * edge looking for undersized or minimum nodes, and arranging
     * for them to be above minimum size.
     */
    while (n) {
	int edge, next, elt, size_e, size_n, size_total;
	nodeptr ne, nn, nl, nr;
	bt_element_t el;

	nodes[nnodes++] = n;

	if (rhs) {
	    edge = bt_subtrees(bt, n) - 1;
	    next = edge - 1;
	    elt = next;
	} else {
	    edge = 0;
	    next = 1;
	    elt = edge;
	}

	ne = bt_write_lock_child(bt, n, edge);
	if (!ne)
	    break;

	size_e = bt_subtrees(bt, ne);

	if (size_e <= bt_min_subtrees(bt)) {
	    nn = bt_write_lock_child(bt, n, next);
	    el = bt_element(bt, n, elt);
	    size_n = bt_subtrees(bt, nn);
	    if (edge < next)
		nl = ne, nr = nn;
	    else
		nl = nn, nr = ne;
	    size_total = size_e + size_n;
	    if (size_e + size_n <= bt_max_subtrees(bt)) {
		/*
		 * Merge the edge node and its sibling together.
		 */
		bt_xform(bt, NODE_AS_IS, 0, nl, nr, el,
			 NODE_ADDR_NULL, NODE_ADDR_NULL,
			 NODE_JOIN, &ne, NULL, NULL);
		bt_xform(bt, NODE_DEL_ELT, elt, n, NULL, NULL,
			 bt_node_addr(bt, ne), NODE_ADDR_NULL,
			 NODE_JOIN, &n, NULL, NULL);
		/*
		 * It's possible we've just trashed the root of the
		 * tree, again.
		 */
		if (bt_subtrees(bt, n) == 1) {
		    bt_shift_root(bt, n, bt_node_addr(bt, ne));
		    nnodes--;	       /* and take it out of nodes[] */
		}
	    } else {
		/*
		 * Redistribute subtrees between the edge node and
		 * its sibling.
		 */
		int split;
		size_e = (size_total + 1) / 2;
		assert(size_e > bt_min_subtrees(bt));
		if (next < edge)
		    split = size_total - size_e - 1;
		else
		    split = size_e - 1;
		bt_xform(bt, NODE_AS_IS, 0, nl, nr, el,
			 NODE_ADDR_NULL, NODE_ADDR_NULL,
			 split, &nl, &nr, &el);
		bt_write_unlock(bt, nn);
		bt_set_element(bt, n, elt, el);
	    }
	}

	n = ne;
    }

    /*
     * Now we just need to go back up and unlock any remaining
     * nodes.
     */
    while (nnodes-- > 0)
	bt_write_unlock(bt, nodes[nnodes]);

    ifree(nodes);
}

/*
 * Split a tree by numeric position. The new tree returned is the
 * one on the right; the original tree contains the stuff on the
 * left.
 */
static btree *bt_split_internal(btree *bt1, int index)
{
    btree *bt2;
    nodeptr *lnodes, *rnodes;
    nodeptr n1, n2, n;
    int nnodes, child;

    bt2 = bt_new(bt1->cmp, bt1->copy, bt1->freeelt, bt1->propsize,
		 bt1->propalign, bt1->propmake, bt1->propmerge,
		 bt1->userstate, bt1->mindegree);
    bt2->depth = bt1->depth;

    lnodes = inewn(nodeptr, bt1->depth);
    rnodes = inewn(nodeptr, bt2->depth);
    nnodes = 0;

    n1 = bt_write_lock_root(bt1);
    while (n1) {
	child = bt_lookup_pos(bt1, n1, &index, NULL);
	n = bt_write_lock_child(bt1, n1, child);
	bt_xform(bt1, NODE_ADD_ELT, child, n1, NULL, NULL,
		 bt_node_addr(bt1, n), NODE_ADDR_NULL,
		 child, &n1, &n2, NULL);
	lnodes[nnodes] = n1;
	rnodes[nnodes] = n2;
	if (nnodes > 0)
	    bt_set_child(bt2, rnodes[nnodes-1], 0, bt_node_addr(bt2, n2));
	else
	    bt2->root = bt_node_addr(bt2, n2);
	nnodes++;
	n1 = n;
    }

    /*
     * Now we go back up and unlock all the nodes. At this point we
     * don't mess with user properties, because there's the danger
     * of a node containing no subtrees _or_ elements and hence us
     * having to invent a notation for an empty property. We're
     * going to make a second healing pass in a moment anyway,
     * which will sort all that out for us.
     */
    while (nnodes-- > 0) {
	bt_write_unlock_internal(bt1, lnodes[nnodes], FALSE);
	bt_write_unlock_internal(bt2, rnodes[nnodes], FALSE);
    }

    /*
     * Then we make a healing pass down each side of the tree.
     */
    bt_split_heal(bt1, TRUE);
    bt_split_heal(bt2, FALSE);

    ifree(lnodes);
    ifree(rnodes);

    return bt2;
}

/*
 * Split a tree at a numeric index.
 */
btree *bt_splitpos(btree *bt, int index, int before)
{
    btree *ret;
    node_addr na;
    int count, nd;
    nodeptr n;

    n = bt_read_lock_root(bt);
    count = (n ? bt_node_count(bt, n) : 0);
    bt_read_unlock(bt, n);

    if (index < 0 || index > count)
	return NULL;

    ret = bt_split_internal(bt, index);
    if (before) {
	na = bt->root;
	bt->root = ret->root;
	ret->root = na;

	nd = bt->depth;
	bt->depth = ret->depth;
	ret->depth = nd;
    }
    return ret;
}

/*
 * Split a tree at a position dictated by the sorting order.
 */
btree *bt_split(btree *bt, bt_element_t element, cmpfn_t cmp, int rel)
{
    int before, index;

    assert(rel != BT_REL_EQ);	       /* has to be an inequality */

    if (rel == BT_REL_GT || rel == BT_REL_GE) {
	before = TRUE;
	rel = (rel == BT_REL_GT ? BT_REL_LE : BT_REL_LT);
    } else {
	before = FALSE;
    }
    if (!bt_findrelpos(bt, element, cmp, rel, &index))
	index = -1;
    return bt_splitpos(bt, index+1, before);
}

#ifdef TEST

#define TEST_DEGREE 4
#define BT_COPY bt_clone
#define MAXTREESIZE 10000
#define MAXLOCKS 100

int errors;

/*
 * Error reporting function.
 */
void error(char *fmt, ...) {
    va_list ap;
    fprintf(stderr, "ERROR: ");
    va_start(ap, fmt);
    vfprintf(stderr, fmt, ap);
    va_end(ap);
    fprintf(stderr, "\n");
    errors++;
}

/*
 * See if a tree has a 2-element root node.
 */
static int bt_tworoot(btree *bt)
{
    nodeptr n;
    int i;
    n = bt_read_lock_root(bt);
    i = bt_subtrees(bt, n);
    bt_read_unlock(bt, n);
    return (i == 2 ? TRUE : FALSE);
}

/*
 * Physically copy an entire B-tree. (NB this appears as a test
 * routine rather than a production one, since reference counting
 * and bt_clone() provide a better way to do this for real code. If
 * anyone really needs a genuine physical copy for anything other
 * than testing reasons, I suppose they could always lift this into
 * the admin section above.)
 */

static nodeptr bt_copy_node(btree *bt, nodeptr n)
{
    int i, children;
    nodeptr ret;

    children = bt_subtrees(bt, n);
    ret = bt_new_node(bt, children);

    for (i = 0; i < children; i++) {
	nodeptr n2 = bt_read_lock_child(bt, n, i);
	nodeptr n3;
	if (n2) {
	    n3 = bt_copy_node(bt, n2);
	    bt_set_child(bt, ret, i, bt_write_unlock(bt, n3));
	} else {
	    bt_set_child(bt, ret, i, NODE_ADDR_NULL);
	}
	bt_read_unlock(bt, n2);
	
	if (i < children-1) {
	    bt_element_t e = bt_element(bt, n, i);
	    if (bt->copy)
		e = bt->copy(bt->userstate, e);
	    bt_set_element(bt, ret, i, e);
	}
    }

    return ret;
}

btree *bt_copy(btree *bt)
{
    nodeptr n;
    btree *bt2;

    bt2 = bt_new(bt->cmp, bt->copy, bt->freeelt, bt->propsize, bt->propalign,
		 bt->propmake, bt->propmerge, bt->userstate, bt->mindegree);
    bt2->depth = bt->depth;

    n = bt_read_lock_root(bt);
    if (n)
	bt2->root = bt_write_unlock(bt2, bt_copy_node(bt, n));
    bt_read_unlock(bt, n);

    return bt2;
}

/*
 * This function is intended to be called from gdb when debugging
 * things.
 */
void bt_dump_nodes(btree *bt, ...)
{
    int i, children;
    va_list ap;
    nodeptr n;

    va_start(ap, bt);
    while (1) {
	n = va_arg(ap, nodeptr);
	if (!n)
	    break;
	printf("%p [%d]:", n, n[bt->maxdegree*2+1].i);
	children = bt_subtrees(bt, n);
	for (i = 0; i < children; i++) {
	    printf(" %p", bt_child(bt, n, i).p);
	    if (i < children-1)
		printf(" %s", (char *)bt_element(bt, n, i));
	}
	printf("\n");
    }
    va_end(ap);
}

/*
 * Verify a tree against an array. Checks that:
 * 
 *  - every node has a valid number of subtrees
 *  - subtrees are either all present (internal node) or all absent
 *    (leaf)
 *  - elements are all present
 *  - every leaf is at exactly the depth claimed by the tree
 *  - the tree represents the correct list of elements in the
 *    correct order. (This also tests the ordering constraint,
 *    assuming the array is correctly constructed.)
 */

void verifynode(btree *bt, nodeptr n, bt_element_t *array, int *arraypos,
		int depth)
{
    int subtrees, min, max, i, before, after, count;

    /* Check the subtree count. The root can have as few as 2 subtrees. */
    subtrees = bt_subtrees(bt, n);
    max = bt_max_subtrees(bt);
    min = (depth == 1) ? 2 : bt_min_subtrees(bt);
    if (subtrees > max)
	error("node %p has too many subtrees (%d > %d)", n, subtrees, max);
    if (subtrees < min)
	error("node %p has too few subtrees (%d < %d)", n, subtrees, min);

    /* Check that subtrees are present or absent as required. */
    for (i = 0; i < subtrees; i++) {
	node_addr child = bt_child(bt, n, i);
	if (depth == bt->depth && child.p != NULL)
	    error("leaf node %p child %d is %p not NULL\n", n, i, child);
	if (depth != bt->depth && child.p == NULL)
	    error("non-leaf node %p child %d is NULL\n", n, i);
    }

    /* Check that elements are all present. */
    for (i = 0; i < subtrees-1; i++) {
	bt_element_t elt = bt_element(bt, n, i);
	if (elt == NULL)
	    error("node %p element %d is NULL\n", n, i);
    }

    before = *arraypos;

    /* Now verify the subtrees, and simultaneously check the ordering. */
    for (i = 0; i < subtrees; i++) {
	if (depth < bt->depth) {
	    nodeptr child = bt_read_lock_child(bt, n, i);
	    verifynode(bt, child, array, arraypos, depth+1);
	    bt_read_unlock(bt, child);
	}
	if (i < subtrees-1) {
	    bt_element_t elt = bt_element(bt, n, i);
	    if (array[*arraypos] != elt) {
		error("node %p element %d is \"%s\", but array[%d]=\"%s\"",
		      n, i, elt, *arraypos, array[*arraypos]);
	    }
	    (*arraypos)++;
	}
    }

    after = *arraypos;

    /* Check the node count. */
    count = bt_node_count(bt, n);
    if (count != after - before)
	error("node %p count is %d, should be %d", n, count, after - before);

    /*
     * Check the user properties.
     */
    {
	nodecomponent *prop;
	int i;
	int max = 0, total = 0;

	prop = n + bt->maxdegree * 2 + 2;

	for (i = before; i < after; i++) {
	    int c = (unsigned char)*(char *)array[i];

	    if (max < c) max = c;
	    total += c;
	}

	if (prop[0].i != total)
	    error("node %p total prop is %d, should be %d", n,
		  prop[0].i, total);
	if (prop[1].i != max)
	    error("node %p max prop is %d, should be %d", n,
		  prop[1].i, max);
    }
}

void verifytree(btree *bt, bt_element_t *array, int arraylen)
{
    nodeptr n;
    int i = 0;
    n = bt_read_lock_root(bt);
    if (n) {
	verifynode(bt, n, array, &i, 1);
	bt_read_unlock(bt, n);
    } else {
	if (bt->depth != 0) {
	    error("tree has null root but depth is %d not zero", bt->depth);
	}
    }
    if (i != arraylen)
	error("tree contains %d elements, array contains %d",
	      i, arraylen);
    testlock(-1, 0, NULL);
}

int mycmp(void *state, void *av, void *bv) {
    char const *a = (char const *)av;
    char const *b = (char const *)bv;
    return strcmp(a, b);
}

static void set_invalid_property(void *propv)
{
    int *prop = (int *)propv;
    prop[0] = prop[1] = -1;
}

void mypropmake(void *state, void *av, void *destv)
{
    char const *a = (char const *)av;
    int *dest = (int *)destv;
    dest[0] = dest[1] = (unsigned char)*a;
}

void mypropmerge(void *state, void *s1v, void *s2v, void *destv)
{
    int *s1 = (int *)s1v;
    int *s2 = (int *)s2v;
    int *dest = (int *)destv;
    if (!s1v && !s2v) {
	/* Special `destroy' case. */
	set_invalid_property(destv);
	return;
    }
    assert(s2[0] >= 0 && s2[1] >= 0);
    assert(s1 == NULL || (s1[0] >= 0 && s1[1] >= 0));
    dest[0] = s2[0] + (s1 ? s1[0] : 0);
    dest[1] = (s1 && s1[1] > s2[1] ? s1[1] : s2[1]);
}

void array_addpos(bt_element_t *array, int *arraylen, bt_element_t e, int i)
{
    bt_element_t e2;
    int len = *arraylen;

    assert(len < MAXTREESIZE);

    while (i < len) {
	e2 = array[i];
	array[i] = e;
	e = e2;
	i++;
    }
    array[len] = e;
    *arraylen = len+1;
}

void array_add(bt_element_t *array, int *arraylen, bt_element_t e)
{
    int i;
    int len = *arraylen;

    for (i = 0; i < len; i++)
	if (mycmp(NULL, array[i], e) >= 0)
	    break;
    assert(i == len || mycmp(NULL, array[i], e) != 0);
    array_addpos(array, arraylen, e, i);
}

void array_delpos(bt_element_t *array, int *arraylen, int i)
{
    int len = *arraylen;

    while (i < len-1) {
	array[i] = array[i+1];
	i++;
    }
    *arraylen = len-1;
}

bt_element_t array_del(bt_element_t *array, int *arraylen, bt_element_t e)
{
    int i;
    int len = *arraylen;
    bt_element_t ret;

    for (i = 0; i < len; i++)
	if (mycmp(NULL, array[i], e) >= 0)
	    break;
    if (i < len && mycmp(NULL, array[i], e) == 0) {
	ret = array[i];
	array_delpos(array, arraylen, i);
    } else
	ret = NULL;
    return ret;
}

/* A sample data set and test utility. Designed for pseudo-randomness,
 * and yet repeatability. */

/*
 * This random number generator uses the `portable implementation'
 * given in ANSI C99 draft N869. It assumes `unsigned' is 32 bits;
 * change it if not.
 */
int randomnumber(unsigned *seed) {
    *seed *= 1103515245;
    *seed += 12345;
    return ((*seed) / 65536) % 32768;
}

#define lenof(x) ( sizeof((x)) / sizeof(*(x)) )

char *strings[] = {
    "0", "2", "3", "I", "K", "d", "H", "J", "Q", "N", "n", "q", "j", "i",
    "7", "G", "F", "D", "b", "x", "g", "B", "e", "v", "V", "T", "f", "E",
    "S", "8", "A", "k", "X", "p", "C", "R", "a", "o", "r", "O", "Z", "u",
    "6", "1", "w", "L", "P", "M", "c", "U", "h", "9", "t", "5", "W", "Y",
    "m", "s", "l", "4",
};

#define NSTR lenof(strings)

void findtest(btree *tree, bt_element_t *array, int arraylen)
{
    static const int rels[] = {
	BT_REL_EQ, BT_REL_GE, BT_REL_LE, BT_REL_LT, BT_REL_GT
    };
    static const char *const relnames[] = {
	"EQ", "GE", "LE", "LT", "GT"
    };
    int i, j, rel, index;
    char *p, *ret, *realret, *realret2;
    int lo, hi, mid, c;

    for (i = 0; i < (int)NSTR; i++) {
	p = strings[i];
	for (j = 0; j < (int)(sizeof(rels)/sizeof(*rels)); j++) {
	    rel = rels[j];

	    lo = 0; hi = arraylen-1;
	    while (lo <= hi) {
		mid = (lo + hi) / 2;
		c = strcmp(p, array[mid]);
		if (c < 0)
		    hi = mid-1;
		else if (c > 0)
		    lo = mid+1;
		else
		    break;
	    }

	    if (c == 0) {
		if (rel == BT_REL_LT)
		    ret = (mid > 0 ? array[--mid] : NULL);
		else if (rel == BT_REL_GT)
		    ret = (mid < arraylen-1 ? array[++mid] : NULL);
		else
		    ret = array[mid];
	    } else {
		assert(lo == hi+1);
		if (rel == BT_REL_LT || rel == BT_REL_LE) {
		    mid = hi;
		    ret = (hi >= 0 ? array[hi] : NULL);
		} else if (rel == BT_REL_GT || rel == BT_REL_GE) {
		    mid = lo;
		    ret = (lo < arraylen ? array[lo] : NULL);
		} else
		    ret = NULL;
	    }

	    realret = bt_findrelpos(tree, p, NULL, rel, &index);
	    testlock(-1, 0, NULL);
	    if (realret != ret) {
		error("find(\"%s\",%s) gave %s should be %s",
		      p, relnames[j], realret, ret);
	    }
	    if (realret && index != mid) {
		error("find(\"%s\",%s) gave %d should be %d",
		      p, relnames[j], index, mid);
	    }
	    if (realret && rel == BT_REL_EQ) {
		realret2 = bt_index(tree, index);
		if (realret2 != realret) {
		    error("find(\"%s\",%s) gave %s(%d) but %d -> %s",
			  p, relnames[j], realret, index, index, realret2);
		}
	    }
	}
    }

    realret = bt_findrelpos(tree, NULL, NULL, BT_REL_GT, &index);
    testlock(-1, 0, NULL);
    if (arraylen && (realret != array[0] || index != 0)) {
	error("find(NULL,GT) gave %s(%d) should be %s(0)",
	      realret, index, array[0]);
    } else if (!arraylen && (realret != NULL)) {
	error("find(NULL,GT) gave %s(%d) should be NULL",
	      realret, index);
    }

    realret = bt_findrelpos(tree, NULL, NULL, BT_REL_LT, &index);
    testlock(-1, 0, NULL);
    if (arraylen && (realret != array[arraylen-1] || index != arraylen-1)) {
	error("find(NULL,LT) gave %s(%d) should be %s(0)",
	      realret, index, array[arraylen-1]);
    } else if (!arraylen && (realret != NULL)) {
	error("find(NULL,LT) gave %s(%d) should be NULL",
	      realret, index);
    }
}

void splittest(btree *tree, bt_element_t *array, int arraylen)
{
    int i;
    btree *tree3, *tree4;
    for (i = 0; i <= arraylen; i++) {
	printf("splittest: %d\n", i);
	tree3 = BT_COPY(tree);
	testlock(-1, 0, NULL);
	tree4 = bt_splitpos(tree3, i, 0);
	testlock(-1, 0, NULL);
	verifytree(tree3, array, i);
	verifytree(tree4, array+i, arraylen-i);
	bt_join(tree3, tree4);
	testlock(-1, 0, NULL);
	verifytree(tree4, NULL, 0);
	bt_free(tree4);	       /* left empty by join */
	testlock(-1, 0, NULL);
	verifytree(tree3, array, arraylen);
	bt_free(tree3);
	testlock(-1, 0, NULL);
    }
}

/*
 * Called to track read and write locks on nodes.
 */
void testlock(int write, int set, nodeptr n)
{
    static nodeptr readlocks[MAXLOCKS], writelocks[MAXLOCKS];
    static int nreadlocks = 0, nwritelocks = 0;

    int i, rp, wp;

    if (write == -1) {
	/* Called after an operation to ensure all locks are unlocked. */
	if (nreadlocks != 0 || nwritelocks != 0)
	    error("at least one left-behind lock exists!");
	return;
    }

    /* Locking NULL does nothing. Unlocking it is an error. */
    if (n == NULL) {
	if (!set)
	    error("attempting to %s-unlock NULL", write ? "write" : "read");
	return;
    }

    assert(nreadlocks < MAXLOCKS && nwritelocks < MAXLOCKS);

    /* First look for the node in both lock lists. */
    rp = wp = -1;
    for (i = 0; i < nreadlocks; i++)
	if (readlocks[i] == n)
	    rp = i;
    for (i = 0; i < nwritelocks; i++)
	if (writelocks[i] == n)
	    wp = i;

    /* Now diverge based on what we're supposed to be up to. */
    if (set) {
	/* Setting a lock. Should not already be locked in either list. */
	if (rp != -1 || wp != -1) {
	    error("attempt to %s-lock node %p, already %s-locked",
		  (write ? "write" : "read"), n, (rp==-1 ? "write" : "read"));
	}
	if (write)
	    writelocks[nwritelocks++] = n;
	else
	    readlocks[nreadlocks++] = n;
    } else {
	/* Clearing a lock. Should exist in exactly the correct list. */
	if (write && rp != -1)
	    error("attempt to write-unlock node %p which is read-locked", n);
	if (!write && wp != -1)
	    error("attempt to read-unlock node %p which is write-locked", n);
	if (wp != -1) {
	    nwritelocks--;
	    for (i = wp; i < nwritelocks; i++)
		writelocks[i] = writelocks[i+1];
	}
	if (rp != -1) {
	    nreadlocks--;
	    for (i = rp; i < nreadlocks; i++)
		readlocks[i] = readlocks[i+1];
	}
    }
}

int main(void) {
    int in[NSTR];
    int i, j, k;
    int tworoot, tmplen;
    unsigned seed = 0;
    bt_element_t *array;
    int arraylen;
    bt_element_t ret, ret2, item;
    btree *tree, *tree2, *tree3, *tree4;

    setvbuf(stdout, NULL, _IOLBF, 0);
    setvbuf(stderr, NULL, _IOLBF, 0);
    errors = 0;

    for (i = 0; i < (int)NSTR; i++) in[i] = 0;
    array = newn(bt_element_t, MAXTREESIZE);
    arraylen = 0;
    tree = bt_new(mycmp, NULL, NULL, 2*sizeof(int), alignof(int),
		  mypropmake, mypropmerge, NULL, TEST_DEGREE);

    verifytree(tree, array, arraylen);
    for (i = 0; i < 10000; i++) {
        j = randomnumber(&seed);
        j %= NSTR;
        printf("trial: %d\n", i);
        if (in[j]) {
            printf("deleting %s (%d)\n", strings[j], j);
	    ret2 = array_del(array, &arraylen, strings[j]);
	    ret = bt_del(tree, strings[j]);
	    testlock(-1, 0, NULL);
	    assert((bt_element_t)strings[j] == ret && ret == ret2);
	    verifytree(tree, array, arraylen);
	    in[j] = 0;
        } else {
            printf("adding %s (%d)\n", strings[j], j);
	    array_add(array, &arraylen, strings[j]);
	    ret = bt_add(tree, strings[j]);
	    testlock(-1, 0, NULL);
	    assert(strings[j] == ret);
	    verifytree(tree, array, arraylen);
            in[j] = 1;
        }
	/* disptree(tree); */
	findtest(tree, array, arraylen);
    }

    while (arraylen > 0) {
        j = randomnumber(&seed);
        j %= arraylen;
	item = array[j];
	ret2 = array_del(array, &arraylen, item);
	ret = bt_del(tree, item);
	testlock(-1, 0, NULL);
	assert(ret2 == ret);
	verifytree(tree, array, arraylen);
    }

    bt_free(tree);
    testlock(-1, 0, NULL);

    /*
     * Now try an unsorted tree. We don't really need to test
     * delpos because we know del is based on it, so it's already
     * been tested in the above sorted-tree code; but for
     * completeness we'll use it to tear down our unsorted tree
     * once we've built it.
     */
    tree = bt_new(NULL, NULL, NULL, 2*sizeof(int), alignof(int),
		  mypropmake, mypropmerge, NULL, TEST_DEGREE);
    verifytree(tree, array, arraylen);
    for (i = 0; i < 1000; i++) {
	printf("trial: %d\n", i);
	j = randomnumber(&seed);
	j %= NSTR;
	k = randomnumber(&seed);
	k %= bt_count(tree)+1;
	testlock(-1, 0, NULL);
	printf("adding string %s at index %d\n", strings[j], k);
	array_addpos(array, &arraylen, strings[j], k);
	bt_addpos(tree, strings[j], k);
	testlock(-1, 0, NULL);
	verifytree(tree, array, arraylen);
    }

    /*
     * While we have this tree in its full form, we'll take a copy
     * of it to use in split and join testing.
     */
    tree2 = BT_COPY(tree);
    testlock(-1, 0, NULL);
    verifytree(tree2, array, arraylen);/* check the copy is accurate */
    /*
     * Split tests. Split the tree at every possible point and
     * check the resulting subtrees.
     */
    tworoot = bt_tworoot(tree2);       /* see if it has a 2-root */
    testlock(-1, 0, NULL);
    splittest(tree2, array, arraylen);
    /*
     * Now do the split test again, but on a tree that has a 2-root
     * (if the previous one didn't) or doesn't (if the previous one
     * did).
     */
    tmplen = arraylen;
    while (bt_tworoot(tree2) == tworoot) {
	bt_delpos(tree2, --tmplen);
	testlock(-1, 0, NULL);
    }
    printf("now trying splits on second tree\n");
    splittest(tree2, array, tmplen);
    bt_free(tree2);
    testlock(-1, 0, NULL);

    /*
     * Back to the main testing of uncounted trees.
     */
    while (bt_count(tree) > 0) {
	printf("cleanup: tree size %d\n", bt_count(tree));
	j = randomnumber(&seed);
	j %= bt_count(tree);
	printf("deleting string %s from index %d\n", (char *)array[j], j);
	ret = bt_delpos(tree, j);
	testlock(-1, 0, NULL);
	assert((bt_element_t)array[j] == ret);
	array_delpos(array, &arraylen, j);
	verifytree(tree, array, arraylen);
    }
    bt_free(tree);
    testlock(-1, 0, NULL);

    /*
     * Finally, do some testing on split/join on _sorted_ trees. At
     * the same time, we'll be testing split on very small trees.
     */
    tree = bt_new(mycmp, NULL, NULL, 2*sizeof(int), alignof(int),
		  mypropmake, mypropmerge, NULL, TEST_DEGREE);
    arraylen = 0;
    for (i = 0; i < 16; i++) {
	array_add(array, &arraylen, strings[i]);
	ret = bt_add(tree, strings[i]);
	testlock(-1, 0, NULL);
	assert(strings[i] == ret);
	verifytree(tree, array, arraylen);
	tree2 = BT_COPY(tree);
	splittest(tree2, array, arraylen);
	testlock(-1, 0, NULL);
	bt_free(tree2);
	testlock(-1, 0, NULL);
    }
    bt_free(tree);
    testlock(-1, 0, NULL);

    /*
     * Test silly cases of join: join(emptytree, emptytree), and
     * also ensure join correctly spots when sorted trees fail the
     * ordering constraint.
     */
    tree = bt_new(mycmp, NULL, NULL, 2*sizeof(int), alignof(int),
		  mypropmake, mypropmerge, NULL, TEST_DEGREE);
    tree2 = bt_new(mycmp, NULL, NULL, 2*sizeof(int), alignof(int),
		   mypropmake, mypropmerge, NULL, TEST_DEGREE);
    tree3 = bt_new(mycmp, NULL, NULL, 2*sizeof(int), alignof(int),
		   mypropmake, mypropmerge, NULL, TEST_DEGREE);
    tree4 = bt_new(mycmp, NULL, NULL, 2*sizeof(int), alignof(int),
		   mypropmake, mypropmerge, NULL, TEST_DEGREE);
    assert(mycmp(NULL, strings[0], strings[1]) < 0);   /* just in case :-) */
    bt_add(tree2, strings[1]);
    testlock(-1, 0, NULL);
    bt_add(tree4, strings[0]);
    testlock(-1, 0, NULL);
    array[0] = strings[0];
    array[1] = strings[1];
    verifytree(tree, array, 0);
    verifytree(tree2, array+1, 1);
    verifytree(tree3, array, 0);
    verifytree(tree4, array, 1);

    /*
     * So:
     *  - join(tree,tree3) should leave both tree and tree3 unchanged.
     *  - joinr(tree,tree2) should leave both tree and tree2 unchanged.
     *  - join(tree4,tree3) should leave both tree3 and tree4 unchanged.
     *  - join(tree, tree2) should move the element from tree2 to tree.
     *  - joinr(tree4, tree3) should move the element from tree4 to tree3.
     *  - join(tree,tree3) should return NULL and leave both unchanged.
     *  - join(tree3,tree) should work and create a bigger tree in tree3.
     */
    assert(tree == bt_join(tree, tree3));
    testlock(-1, 0, NULL);
    verifytree(tree, array, 0);
    verifytree(tree3, array, 0);
    assert(tree2 == bt_joinr(tree, tree2));
    testlock(-1, 0, NULL);
    verifytree(tree, array, 0);
    verifytree(tree2, array+1, 1);
    assert(tree4 == bt_join(tree4, tree3));
    testlock(-1, 0, NULL);
    verifytree(tree3, array, 0);
    verifytree(tree4, array, 1);
    assert(tree == bt_join(tree, tree2));
    testlock(-1, 0, NULL);
    verifytree(tree, array+1, 1);
    verifytree(tree2, array, 0);
    assert(tree3 == bt_joinr(tree4, tree3));
    testlock(-1, 0, NULL);
    verifytree(tree3, array, 1);
    verifytree(tree4, array, 0);
    assert(NULL == bt_join(tree, tree3));
    testlock(-1, 0, NULL);
    verifytree(tree, array+1, 1);
    verifytree(tree3, array, 1);
    assert(tree3 == bt_join(tree3, tree));
    testlock(-1, 0, NULL);
    verifytree(tree3, array, 2);
    verifytree(tree, array, 0);

    bt_free(tree);
    testlock(-1, 0, NULL);
    bt_free(tree2);
    testlock(-1, 0, NULL);
    bt_free(tree3);
    testlock(-1, 0, NULL);
    bt_free(tree4);
    testlock(-1, 0, NULL);

    sfree(array);

    if (errors)
	fprintf(stderr, "%d errors!\n", errors);
    return (errors != 0 ? 1 : 0);
}

#endif
