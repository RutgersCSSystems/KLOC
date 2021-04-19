#ifndef _LINUX_PFN_TRACE_H
#define _LINUC_PFN_TRACE_H

#include <linux/hashtable.h>


struct pfn_node {
	unsigned long pfn_val;
	struct hlist_node next;
};

extern void insert_pfn_hashtable(unsigned long pfn);
extern void print_pfn_hashtable(void);

#endif
