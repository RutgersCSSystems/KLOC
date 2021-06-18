### Annotation mechanism for heterogeneous memory

* Adding hetero object for a structure
```
#ifdef CONFIG_HETERO_ENABLE
        void *hetero_obj;
#endif
```


```
#ifdef CONFIG_HETERO_ENABLE
        if (is_hetero_obj(hetero_obj)){
	
        }
#endif
```

* Mark the file mapping to Hetero target object

```
/*Mark the mapping to Hetero target object*/
#ifdef CONFIG_HETERO_ENABLE
        set_fsmap_hetero_obj(inode->i_mapping);
#endif
```

* Mark the socket to Hetero target object
```
/*Mark the socket to Hetero target object*/
#ifdef CONFIG_HETERO_ENABLE
        set_sock_hetero_obj(sock);
#endif
```

* Setting socket objects
```
#ifdef CONFIG_HETERO_ENABLE
        if ((is_hetero_buffer_set() || is_hetero_pgcache_set())
                && file->f_inode ) {
                set_socket_hetero_obj((void *)sock, (void *)file->f_inode);
        }
#endif
```

* Setting stream_alloc_skb
```
#ifdef CONFIG_HETERO_ENABLE
        skb = NULL;
        if(sk && (is_hetero_obj(sk->hetero_obj))){
		...
        }
        if(!skb)
#endif
```


