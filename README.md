fuse-rofiles
=========

Create a mountpoint that represents an underlying directory hierarchy,
but where non-directory inodes cannot have content or xattrs changed.
Files can still be unlinked, and new ones created.

This filesystem is designed for OSTree - it's useful for computing
overlay filesystems based on underlying hardlinked trees.  

Based on https://github.com/cognusion/fuse-rofs
Copyright 2005,2006,2008 Matthew Keller. m@cognusion.com and others.

Original content from fuse-rofs:

    > From: fuse-rofs
    > I read (and borrowed) a lot of other FUSE code to write this. 
    > Similarities possibly exist- Wholesale reuse as well of other GPL code.
    > Special mention to Rémi Flament and his loggedfs.
 
