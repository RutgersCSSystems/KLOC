#include <linux/rbtree_augmented.h>
#include <linux/export.h>
#include <asm/page.h>
#include <linux/bootmem.h>
#include <linux/slab.h>
#include <linux/mm.h>
#include <linux/mm_inline.h>
#include <linux/pfn_trace.h>
#include <linux/hashtable.h>
#include <linux/kernel.h>


#define PFN_BIT 24

DEFINE_HASHTABLE(pfn_table, PFN_BIT);

void insert_pfn_hashtable(unsigned long pfn) {
	unsigned long key;

	struct pfn_node *p_node = (struct pfn_node *)kmalloc(sizeof(*p_node), GFP_ATOMIC);
	if (!p_node)
		printk("Allocation error! \n");

	p_node->pfn_val = pfn;

	key = pfn % max_pfn;
//	printk(KERN_ALERT "hash add before \n");
	hash_add(pfn_table, &p_node->next, key);
//	printk(KERN_ALERT "hash add after \n");
//	print_pfn_hashtable();
}
EXPORT_SYMBOL(insert_pfn_hashtable);

void print_pfn_hashtable(void) {
	unsigned long bkt;

	struct pfn_node *cur;
	int cnt;

//	printk(KERN_ALERT "This is print_pfn_hashtable function \n");

	for ((bkt) = 0, cur = NULL; cur==NULL && (bkt) < HASH_SIZE(pfn_table); (bkt)++) {
		cnt = 0;
		hlist_for_each_entry(cur, &pfn_table[bkt], next) {
//			printk("data: %lu is in bucket %lu", cur->pfn_val, bkt);
			cnt++;
		}
		if (cnt != 0)
			printk("pfn %lu: %d\n", bkt, cnt);
	}
}
EXPORT_SYMBOL(print_pfn_hashtable);
