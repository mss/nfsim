s/elem->hook(/call_elem_hook(elem, /
s/^static LIST_HEAD(nf_sockopts);/LIST_HEAD(nf_sockopts);/
s/^int nf_register_sockopt(/int __nf_register_sockopt(/
s/^int nf_register_hook(/int __nf_register_hook(/
s/^int nf_register_hooks(/int __nf_register_hooks(/
s/^void nf_unregister_sockopt(/void __nf_unregister_sockopt(/
s/^void nf_unregister_hook(/void __nf_unregister_hook(/
s/^void nf_unregister_hooks(/void __nf_unregister_hooks(/
s/^int skb_ip_make_writable(/int __unused_skb_ip_make_writable(/
s/^int skb_make_writable/int __unused_skb_make_writable/
