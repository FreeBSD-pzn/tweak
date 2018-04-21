#include "tweak.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static DFA build_dfa (char *pattern, int len)
{
    int i, j, k, b;
    char *tmp = malloc(len);
    DFA dfa = malloc(len * sizeof(*dfa));

    if (!dfa)
	return NULL;
    if (!tmp)
	return NULL;

    memcpy (tmp, pattern, len);

    for (i=len; i-- ;) {
	j = i+1;
	for (b=0; b<256; b++) {
	    dfa[i][b] = 0;
	    if (memchr(pattern, b, len)) {
		tmp[j-1] = b;
		for (k=1; k<=j; k++)
		    if (!memcmp(tmp+j-k, pattern, k))
			dfa[i][b] = k;
	    }
	}
    }

    return dfa;
}

Search *build_search(char *pattern, int len)
{
    Search *ret = malloc(sizeof(Search));
    char *revpat = malloc(len);
    int i;

    ret->len = len;
    ret->forward = build_dfa(pattern, len);
    for (i = 0; i < len; i++)
	revpat[i] = pattern[len-1-i];
    ret->reverse = build_dfa(revpat, len);

    return ret;
}

void free_search(Search *s)
{
    free(s->forward);
    free(s->reverse);
    free(s);
}
