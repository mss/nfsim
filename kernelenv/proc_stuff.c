/*

Copyright (c) 2003,2004 Jeremy Kerr & Rusty Russell

This file is part of nfsim.

nfsim is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.

nfsim is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with nfsim; if not, write to the Free Software
Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
*/

#include <core.h>
#include <kernelenv.h>

struct proc_dir_entry proc_root, *proc_net, *proc_net_stat, *proc_sys_root;
static void *__proc_ctx = NULL;

int proc_match(int len, const char *name, struct proc_dir_entry *de)
{
	if (de->namelen != len)
		return 0;
	return !memcmp(name, de->name, len);
}

static int xlate_proc_name(const char *name,
			   struct proc_dir_entry **ret, const char **residual)
{
	const char     		*cp = name, *next;
	struct proc_dir_entry	*de;
	int			len;

	de = &proc_root;
	while (1) {
		next = strchr(cp, '/');
		if (!next)
			break;

		len = next - cp;
		for (de = de->subdir; de ; de = de->next) {
			if (proc_match(len, cp, de))
				break;
		}
		if (!de)
			return -ENOENT;
		cp += len + 1;
	}
	*residual = cp;
	*ret = de;
	return 0;
}

/* Stolen straight from kernel's fs/proc/base.c,
 *  Copyright (C) 1991, 1992 Linus Torvalds
 */
/* buffer size is one page but our output routines use some slack for overruns */
#define PROC_BLOCK_SIZE	(PAGE_SIZE - 1024)

static ssize_t
proc_file_read(struct file *file, char *buf, size_t nbytes,
	       loff_t *ppos)
{
	struct inode * inode = file->f_dentry->d_inode;
	ssize_t	retval=0;
	int	eof=0;
	ssize_t	n, count;
	char	*start;
	char page[PAGE_SIZE];
	struct proc_dir_entry * dp;

	dp = PDE(inode);
	while ((nbytes > 0) && !eof) {
		count = min_t(ssize_t, PROC_BLOCK_SIZE, nbytes);

		start = NULL;
		if (dp->get_info) {
			/* Handle old net routines */
			n = dp->get_info(page, &start, *ppos, count);
			if (n < count)
				eof = 1;
		} else if (dp->read_proc) {
			/*
			 * How to be a proc read function
			 * ------------------------------
			 * Prototype:
			 *    int f(char *buffer, char **start, off_t offset,
			 *          int count, int *peof, void *dat)
			 *
			 * Assume that the buffer is "count" bytes in size.
			 *
			 * If you know you have supplied all the data you
			 * have, set *peof.
			 *
			 * You have three ways to return data:
			 * 0) Leave *start = NULL.  (This is the default.)
			 *    Put the data of the requested offset at that
			 *    offset within the buffer.  Return the number (n)
			 *    of bytes there are from the beginning of the
			 *    buffer up to the last byte of data.  If the
			 *    number of supplied bytes (= n - offset) is 
			 *    greater than zero and you didn't signal eof
			 *    and the reader is prepared to take more data
			 *    you will be called again with the requested
			 *    offset advanced by the number of bytes 
			 *    absorbed.  This interface is useful for files
			 *    no larger than the buffer.
			 * 1) Set *start = an unsigned long value less than
			 *    the buffer address but greater than zero.
			 *    Put the data of the requested offset at the
			 *    beginning of the buffer.  Return the number of
			 *    bytes of data placed there.  If this number is
			 *    greater than zero and you didn't signal eof
			 *    and the reader is prepared to take more data
			 *    you will be called again with the requested
			 *    offset advanced by *start.  This interface is
			 *    useful when you have a large file consisting
			 *    of a series of blocks which you want to count
			 *    and return as wholes.
			 *    (Hack by Paul.Russell@rustcorp.com.au)
			 * 2) Set *start = an address within the buffer.
			 *    Put the data of the requested offset at *start.
			 *    Return the number of bytes of data placed there.
			 *    If this number is greater than zero and you
			 *    didn't signal eof and the reader is prepared to
			 *    take more data you will be called again with the
			 *    requested offset advanced by the number of bytes
			 *    absorbed.
			 */
			n = dp->read_proc(page, &start, *ppos,
					  count, &eof, dp->data);
		} else
			break;

		if (n == 0)   /* end of file */
			break;
		if (n < 0) {  /* error */
			if (retval == 0)
				retval = n;
			break;
		}

		if (start == NULL) {
			if (n > PAGE_SIZE) {
				printk(KERN_ERR
				       "proc_file_read: Apparent buffer overflow!\n");
				n = PAGE_SIZE;
			}
			n -= *ppos;
			if (n <= 0)
				break;
			if (n > count)
				n = count;
			start = page + *ppos;
		} else if (start < page) {
			if (n > PAGE_SIZE) {
				printk(KERN_ERR
				       "proc_file_read: Apparent buffer overflow!\n");
				n = PAGE_SIZE;
			}
			if (n > count) {
				/*
				 * Don't reduce n because doing so might
				 * cut off part of a data block.
				 */
				printk(KERN_WARNING
				       "proc_file_read: Read count exceeded\n");
			}
		} else /* start >= page */ {
			unsigned long startoff = (unsigned long)(start - page);
			if (n > (PAGE_SIZE - startoff)) {
				printk(KERN_ERR
				       "proc_file_read: Apparent buffer overflow!\n");
				n = PAGE_SIZE - startoff;
			}
			if (n > count)
				n = count;
		}

		memcpy(buf, start < page ? page : start, n);

		*ppos += start < page ? (unsigned long)start : n;
		nbytes -= n;
		buf += n;
		retval += n;
	}
	return retval;
}

static ssize_t
proc_file_write(struct file *file, const char *buffer,
		size_t count, loff_t *ppos)
{
	struct inode *inode = file->f_dentry->d_inode;
	struct proc_dir_entry * dp;
	
	dp = PDE(inode);

	if (!dp->write_proc)
		return -EIO;

	/* FIXME: does this routine need ppos?  probably... */
	return dp->write_proc(file, buffer, count, dp->data);
}

static struct file_operations proc_file_operations = {
	.read		= proc_file_read,
	.write		= proc_file_write,
};

static struct proc_dir_entry *proc_create(struct proc_dir_entry **parent,
					  const char *name,
					  mode_t mode,
					  nlink_t nlink)
{
	struct proc_dir_entry *ent = NULL;
	const char *fn = name;
	int len;

	/* make sure name is valid */
	if (!name || !strlen(name)) goto out;

	if (!(*parent) && xlate_proc_name(name, parent, &fn) != 0)
		goto out;
	len = strlen(fn);

	if (should_i_fail(__func__))
		goto out;

	ent = talloc(__proc_ctx, struct proc_dir_entry);
	ent->name = talloc_strdup(ent, name);
	ent->namelen = len;
	ent->get_info = NULL;
	ent->owner = NULL;
	ent->data = NULL;
	ent->mode = mode;
	ent->proc_fops = &proc_file_operations;
	ent->read_proc = NULL;
	ent->write_proc = NULL;
	ent->subdir = NULL;
out:
	return ent;
}

static int proc_register(struct proc_dir_entry * dir, struct proc_dir_entry * dp)
{
	dp->next = dir->subdir;
	dp->parent = dir;
	dir->subdir = dp;
	return 0;
}

struct proc_dir_entry *proc_mkdir(const char *name, struct proc_dir_entry *parent)
{
	struct proc_dir_entry *ent;

	ent = proc_create(&parent,name,
			  (S_IFDIR | S_IRUGO | S_IXUGO),2);

	if (ent && proc_register(parent, ent) < 0) {
		talloc_free(ent);
		ent = NULL;
	}
	return ent;
}

struct proc_dir_entry *create_proc_entry(const char *name, mode_t mode,
					 struct proc_dir_entry *parent)
{
	struct proc_dir_entry *ent;

	ent = proc_create(&parent,name,mode, 0);
	if (ent && proc_register(parent, ent) < 0) {
		talloc_free(ent);
		ent = NULL;
	}
	return ent;
}

/*** XML Argument:
    <section id="a:ignore-proc-issues">
     <title><option>--ignore-proc-issues</option></title>
     <subtitle>Ignore issues with /proc files leaking</subtitle>

     <para>The <option>--ignore-proc-issues</option> can be used to
     suppress failure due to removing non-existent proc entries and
     leaving proc files behind, which happened in 2.6.10 and previous.
     </para>
     </section>
*/
static bool ignore_proc_issues = false;
static void cmdline_ignore_proc_issues(struct option *opt)
{
	ignore_proc_issues = true;
}
cmdline_opt("ignore-proc-issues", 0, 0, cmdline_ignore_proc_issues);

void remove_proc_entry(const char *name, struct proc_dir_entry *parent)
{
	struct proc_dir_entry **p;
	struct proc_dir_entry *de;
	const char *fn = name;
	int len;

	if (!parent && xlate_proc_name(name, &parent, &fn) != 0)
		barf("Cannot remove non-existent entry '%s'", name);
	len = strlen(fn);
	for (p = &parent->subdir; *p; p=&(*p)->next ) {
		if (!proc_match(len, fn, *p))
			continue;
		de = *p;
		*p = de->next;
		talloc_free(de);
		return;
	}
	if (!ignore_proc_issues)
		barf("Cannot find entry '%s' under %s to remove", name,
		     parent ? parent->name : "/");
}

struct proc_dir_entry *create_proc_info_entry(const char *name,
					      mode_t mode,
					      struct proc_dir_entry *base,
					      get_info_t *get_info)
{
	struct proc_dir_entry *res=create_proc_entry(name,mode,base);
	if (res) res->get_info=get_info;
	return res;
}

struct proc_dir_entry *proc_net_create(const char *name,
				       mode_t mode, get_info_t *get_info)
{
	return create_proc_info_entry(name,mode,proc_net,get_info);
}

struct proc_dir_entry *proc_net_fops_create(const char *name,
					    mode_t mode,
					    struct file_operations *fops)
{
	struct proc_dir_entry *res = create_proc_entry(name, mode, proc_net);
	if (res)
		res->proc_fops = fops;
	return res;
}

void proc_net_remove(const char *name)
{
	remove_proc_entry(name,proc_net);
}

static int test_perm(int mode, int op)
{
	mode >>= 6;
	if ((mode & op & 0007) == op)
		return 0;
	return -EACCES;
}

static inline int ctl_perm(ctl_table *table, int op)
{
	return test_perm(table->mode, op);
}

static ssize_t do_rw_proc(int write, struct file * file, char * buf,
			  size_t count, loff_t *ppos)
{
	int op;
	struct proc_dir_entry *de;
	struct ctl_table *table;
	size_t res;
	ssize_t error;
	
	de = PDE(file->f_dentry->d_inode);
	if (!de || !de->data)
		return -ENOTDIR;
	table = (struct ctl_table *) de->data;
	if (!table || !table->proc_handler)
		return -ENOTDIR;
	op = (write ? 002 : 004);
	if (ctl_perm(table, op))
		return -EPERM;
	
	res = count;

	error = (*table->proc_handler) (table, write, file, buf, &res, ppos);
	if (error)
		return error;
	return res;
}

static int proc_opensys(struct inode *inode, struct file *file)
{
	if (file->f_mode & FMODE_WRITE) {
		/*
		 * sysctl entries that are not writable,
		 * are _NOT_ writable, capabilities or not.
		 */
		if (!(inode->i_mode & S_IWUSR))
			return -EPERM;
	}

	return 0;
}

static ssize_t proc_readsys(struct file * file, char * buf,
			    size_t count, loff_t *ppos)
{
	return do_rw_proc(0, file, buf, count, ppos);
}

static ssize_t proc_writesys(struct file * file, const char * buf,
			     size_t count, loff_t *ppos)
{
	return do_rw_proc(1, file, (char *) buf, count, ppos);
}

struct file_operations proc_sys_file_operations = {
	.open		= proc_opensys,
	.read		= proc_readsys,
	.write		= proc_writesys,
};

/* Scan the sysctl entries in table and add them all into /proc */
static void register_proc_table(ctl_table * table, struct proc_dir_entry *root)
{
	struct proc_dir_entry *de;
	int len;
	mode_t mode;
	
	for (; table->ctl_name; table++) {
		/* Can't do anything without a proc name. */
		if (!table->procname)
			continue;
		/* Maybe we can't do anything with it... */
		if (!table->proc_handler && !table->child)
			barf("SYSCTL: Can't register %s\n", table->procname);

		len = strlen(table->procname);
		mode = table->mode;

		de = NULL;
		if (table->proc_handler)
			mode |= S_IFREG;
		else {
			mode |= S_IFDIR;
			for (de = root->subdir; de; de = de->next) {
				if (proc_match(len, table->procname, de))
					break;
			}
			/* If the subdir exists already, de is non-NULL */
		}

		if (!de) {
			de = create_proc_entry(table->procname, mode, root);
			if (!de)
				continue;
			de->data = (void *) table;
			if (table->proc_handler)
				de->proc_fops = &proc_sys_file_operations;
		}
		table->de = de;
		if (de->mode & S_IFDIR)
			register_proc_table(table->child, de);
	}
}

/*
 * Unregister a /proc sysctl table and any subdirectories.
 */
static void unregister_proc_table(ctl_table * table, struct proc_dir_entry *root)
{
	struct proc_dir_entry *de;
	for (; table->ctl_name; table++) {
		if (!(de = table->de))
			continue;
		if (de->mode & S_IFDIR) {
			if (!table->child) {
				printk (KERN_ALERT "Help - malformed sysctl tree on free\n");
				continue;
			}
			unregister_proc_table(table->child, de);

			/* Don't unregister directories which still have entries.. */
			if (de->subdir)
				continue;
		}

		table->de = NULL;
		remove_proc_entry(table->procname, root);
	}
}

struct ctl_table_header
{
	struct ctl_table *ctl_table;
};

struct ctl_table_header *register_sysctl_table(ctl_table* table,
					       int insert_at_head)
{
	struct ctl_table_header *ret;

	if (should_i_fail(__func__))
		return NULL;

	ret = talloc(__proc_ctx, struct ctl_table_header);
	ret->ctl_table = table;

	/* We ignore proc failures here, just like core code does. */
	suppress_failtest++;
	register_proc_table(table, proc_sys_root);
	suppress_failtest--;
	return ret;
}

void unregister_sysctl_table(struct ctl_table_header * header)
{
	unregister_proc_table(header->ctl_table, proc_sys_root);
	talloc_free(header);
}

struct proc_dir_entry *PDE(const struct inode *inode)
{
	return ((struct inode *)inode)->e;
}

static int do_proc_dointvec_conv(int *negp, unsigned long *lvalp,
				 int *valp,
				 int write, void *data)
{
	if (write) {
		*valp = *negp ? -*lvalp : *lvalp;
	} else {
		int val = *valp;
		if (val < 0) {
			*negp = -1;
			*lvalp = (unsigned long)-val;
		} else {
			*negp = 0;
			*lvalp = (unsigned long)val;
		}
	}
	return 0;
}

static int do_proc_dointvec(ctl_table *table, int write, struct file *filp,
		  void *buffer, size_t *lenp, loff_t *ppos,
		  int (*conv)(int *negp, unsigned long *lvalp, int *valp,
			      int write, void *data),
		  void *data)
{
#define TMPBUFLEN 21
	int *i, vleft, first=1, neg, val;
	unsigned long lval;
	size_t left, len;
	
	char buf[TMPBUFLEN], *p;
	char *s = buffer;
	
	if (!table->data || !table->maxlen || !*lenp ||
	    (*ppos && !write)) {
		*lenp = 0;
		return 0;
	}
	
	i = (int *) table->data;
	vleft = table->maxlen / sizeof(*i);
	left = *lenp;

	if (!conv)
		conv = do_proc_dointvec_conv;

	for (; left && vleft--; i++, first=0) {
		if (write) {
			while (left) {
				char c;
				c = *s;
				if (!isspace(c))
					break;
				left--;
				s++;
			}
			if (!left)
				break;
			neg = 0;
			len = left;
			if (len > sizeof(buf) - 1)
				len = sizeof(buf) - 1;
			memcpy(buf, s, len);
			buf[len] = 0;
			p = buf;
			if (*p == '-' && left > 1) {
				neg = 1;
				left--, p++;
			}
			if (*p < '0' || *p > '9')
				break;

			lval = simple_strtoul(p, &p, 0);

			len = p-buf;
			if ((len < left) && *p && !isspace(*p))
				break;
			if (neg)
				val = -val;
			s += len;
			left -= len;

			if (conv(&neg, &lval, i, 1, data))
				break;
		} else {
			p = buf;
			if (!first)
				*p++ = '\t';
	
			if (conv(&neg, &lval, i, 0, data))
				break;

			sprintf(p, "%s%lu", neg ? "-" : "", lval);
			len = strlen(buf);
			if (len > left)
				len = left;
			memcpy(s, buf, len);
			left -= len;
			s += len;
		}
	}

	if (!write && !first && left) {
		*s = '\n';
		left--, s++;
	}
	if (write) {
		while (left) {
			char c;
			c = *(s++);
			if (!isspace(c))
				break;
			left--;
		}
	}
	if (write && first)
		return -EINVAL;
	*lenp -= left;
	*ppos += *lenp;
	return 0;
#undef TMPBUFLEN
}

/**
 * proc_dointvec - read a vector of integers
 * @table: the sysctl table
 * @write: %TRUE if this is a write to the sysctl file
 * @filp: the file structure
 * @buffer: the user buffer
 * @lenp: the size of the user buffer
 *
 * Reads/writes up to table->maxlen/sizeof(unsigned int) integer
 * values from/to the user buffer, treated as an ASCII string. 
 *
 * Returns 0 on success.
 */
int proc_dointvec(ctl_table *table, int write, struct file *filp,
		     void *buffer, size_t *lenp, loff_t *ppos)
{
    return do_proc_dointvec(table,write,filp,buffer,lenp,ppos,
		    	    NULL,NULL);
}

static int do_proc_dointvec_jiffies_conv(int *negp, unsigned long *lvalp,
					 int *valp,
					 int write, void *data)
{
	if (write) {
		*valp = *negp ? -(*lvalp*HZ) : (*lvalp*HZ);
	} else {
		int val = *valp;
		unsigned long lval;
		if (val < 0) {
			*negp = -1;
			lval = (unsigned long)-val;
		} else {
			*negp = 0;
			lval = (unsigned long)val;
		}
		*lvalp = lval / HZ;
	}
	return 0;
}

int proc_dointvec_jiffies(ctl_table *table, int write, struct file *filp,
			  void *buffer, size_t *lenp, loff_t *ppos)
{
    return do_proc_dointvec(table,write,filp,buffer,lenp,ppos,
		    	    do_proc_dointvec_jiffies_conv,NULL);
}

struct do_proc_dointvec_minmax_conv_param {
	int *min;
	int *max;
};

static int do_proc_dointvec_minmax_conv(int *negp, unsigned long *lvalp, 
					int *valp, 
					int write, void *data)
{
	struct do_proc_dointvec_minmax_conv_param *param = data;
	if (write) {
		int val = *negp ? -*lvalp : *lvalp;
		if ((param->min && *param->min > val) ||
		    (param->max && *param->max < val))
			return -EINVAL;
		*valp = val;
	} else {
		int val = *valp;
		if (val < 0) {
			*negp = -1;
			*lvalp = (unsigned long)-val;
		} else {
			*negp = 0;
			*lvalp = (unsigned long)val;
		}
	}
	return 0;
}

/**
 * proc_dointvec_minmax - read a vector of integers with min/max values
 * @table: the sysctl table
 * @write: %TRUE if this is a write to the sysctl file
 * @filp: the file structure
 * @buffer: the user buffer
 * @lenp: the size of the user buffer
 *
 * Reads/writes up to table->maxlen/sizeof(unsigned int) integer
 * values from/to the user buffer, treated as an ASCII string.
 *
 * This routine will ensure the values are within the range specified by
 * table->extra1 (min) and table->extra2 (max).
 *
 * Returns 0 on success.
 */
int proc_dointvec_minmax(ctl_table *table, int write, struct file *filp,
		  void *buffer, size_t *lenp, loff_t *ppos)
{
	struct do_proc_dointvec_minmax_conv_param param = {
		.min = (int *) table->extra1,
		.max = (int *) table->extra2,
	};
	return do_proc_dointvec(table, write, filp, buffer, lenp, ppos,
				do_proc_dointvec_minmax_conv, &param);
}

int sysctl_intvec(ctl_table *table, int *name, int nlen,
		void *oldval, size_t *oldlenp,
		void *newval, size_t newlen, void **context)
{

	if (newval && newlen) {
		int *vec = (int *) newval;
		int *min = (int *) table->extra1;
		int *max = (int *) table->extra2;
		size_t length;
		int i;

		if (newlen % sizeof(int) != 0)
			return -EINVAL;

		if (!table->extra1 && !table->extra2)
			return 0;

		if (newlen > table->maxlen)
			newlen = table->maxlen;
		length = newlen / sizeof(int);

		for (i = 0; i < length; i++) {
			int value = vec[i];
			if (min && value < min[i])
				return -EINVAL;
			if (max && value > max[i])
				return -EINVAL;
		}
	}
	return 0;
}

static struct proc_dir_entry *find_proc_entry(const char *name)
{
	struct proc_dir_entry *ent;
	const char *next;
	int len, last;

	/* strip the /proc/ */
	name += 6;

	ent = &proc_root;

	last = 0;

	while (!last) {
		if ((next = strchr(name, '/'))) {
			len = next - name;
		} else {
			len = strlen(name);
			last = 1;
		}

		for (ent = ent->subdir; ent; ent = ent->next) {
			if (proc_match(len, name, ent))
				break;
		}
		if (!ent)
			return 0;
		name += len + 1;

	}

	return ent;

}

static struct proc_dir_entry *nfsim_proc_open(const char *name,
					      struct file **f,
					      int mode)
{
	struct proc_dir_entry *dir;
	struct inode *inode;
	struct dentry *dentry;

	dir = find_proc_entry(name);

	if (!dir) {
		nfsim_log(LOG_UI, "proc file not found: %s", name);
		return NULL;
	}

	if (S_ISDIR(dir->mode)) {
		nfsim_log(LOG_UI, "proc entry %s is a directory", name);
		return NULL;
	}

	if (mode == FMODE_WRITE && !(dir->mode & 0200)) {
		nfsim_log(LOG_UI, "proc entry %s not writable", name);
		return NULL;
	}

	*f = talloc(NULL, struct file);
	inode = talloc(*f, struct inode);
	dentry = talloc(*f, struct dentry);

	inode->i_mode = dir->mode;
	inode->e = dir;
	dentry->d_inode = inode;

	(*f)->f_mode = mode;
	(*f)->f_dentry = dentry;
	(*f)->f_pos = 0;

	if (dir->proc_fops->open && dir->proc_fops->open(inode, *f) < 0) {
		nfsim_log(LOG_UI, "proc entry %s open failed!", name);
		talloc_free(*f);
		return NULL;
	}
	return dir;
}

static void nfsim_proc_close(struct proc_dir_entry *dir, struct file *f)
{
	if (dir->proc_fops->release)
		dir->proc_fops->release(f->f_dentry->d_inode, f);
	talloc_free(f);
}

bool nfsim_proc_cat(const char *name)
{
	struct file *f;
	struct proc_dir_entry *dir;
	int ret;
	char buf[16384];

	dir = nfsim_proc_open(name, &f, FMODE_READ);
	if (!dir)
		return false;

	ret = dir->proc_fops->read(f, buf, sizeof(buf)-1, &f->f_pos);
	nfsim_proc_close(dir, f);
	if (ret < 0) {
		nfsim_log(LOG_UI, "proc entry %s read failed %i!",
			  name, ret);
		return false;
	}
	printk("%.*s", ret, buf);
	return true;
}

bool nfsim_proc_write(const char *name, char *argv[])
{
	char *str = NULL;
	struct file *f;
	struct proc_dir_entry *dir;
	int ret;

	dir = nfsim_proc_open(name, &f, FMODE_WRITE);
	if (!dir)
		return false;

	while (*argv) {
		str = talloc_asprintf_append(str, "%s%s",
					     *argv, argv[1] ? " " : "");
		argv++;
	}

	ret = dir->proc_fops->write(f, str, strlen(str), &f->f_pos);
	nfsim_proc_close(dir, f);
	talloc_free(str);

	if (ret < 0) {
		nfsim_log(LOG_UI, "proc entry %s write failed %i!",
			  name, ret);
		return false;
	}
	return true;
}

static void proc_init(void)
{
	proc_root.namelen = 5;
	proc_root.name = "/proc";
	proc_root.owner = THIS_MODULE;

	proc_net = proc_mkdir("net", NULL);
	proc_net_stat = proc_mkdir("stat", proc_net);

	proc_sys_root = proc_mkdir("sys", NULL);

	__proc_ctx = talloc_named_const(nfsim_tallocs, 1, "proc entries");
}

void proc_cleanup(void)
{
	if (ignore_proc_issues) {
		suppress_failtest++;
		/* Free and reinitialize, so it's like before. */
		talloc_free(__proc_ctx);
		__proc_ctx = NULL;
		memset(&proc_root, 0, sizeof(proc_root));
		proc_init();
		suppress_failtest--;
	}
}
		
init_call(proc_init);

