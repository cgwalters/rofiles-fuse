#!/bin/bash

set -e
set -x

tmpdir=$(mktemp -d --suffix .instroot)
tmpdir_mnt=$(mktemp -d --suffix .mnt)

repos_opts="--setopt=reposdir=/etc/pkgrepos.library --disablerepo=* --enablerepo=fedora-22-local"

function cleanup() {
    umount ${tmpdir_mnt}/usr/share/rpm 2>/dev/null || true
    umount ${tmpdir_mnt}/usr/share/yum 2>/dev/null || true
    fusermount -u ${tmpdir_mnt} || true
}

function migrate_var_to_usr() {
    root=$1
    test -n "$root" || exit 1
    name=$2
    test -n "$name" || exit 1
    (cd "${root}";
	mv var/lib/${name} usr/share/${name};
	ln -s ../../usr/share/${name} var/lib/${name})
}

function copy_up_dir() {
    dpath=$1
    test -n "${dpath}" || exit 1
    bn=$(basename ${dpath})
    (cd $(dirname ${dpath});
	cp -a ${bn} ${bn}.copy
	rm ${bn} -rf
	mv ${bn}.copy ${bn})
}

yum -y ${repos_opts} --installroot=${tmpdir}/a install bash
migrate_var_to_usr ${tmpdir}/a rpm
migrate_var_to_usr ${tmpdir}/a yum

cp -al ${tmpdir}/{a,b}
copy_up_dir ${tmpdir}/b/usr/share/rpm
copy_up_dir ${tmpdir}/b/usr/share/yum
rofiles-fuse ${tmpdir}/b ${tmpdir_mnt}
mount --bind ${tmpdir}/b/usr/share/rpm ${tmpdir_mnt}/usr/share/rpm
mount --bind ${tmpdir}/b/usr/share/yum ${tmpdir_mnt}/usr/share/yum
trap cleanup EXIT
strace -f -s 2048 -o /tmp/strace.log yum -y ${repos_opts} --installroot=${tmpdir_mnt} install strace

