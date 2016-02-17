#!/bin/sh

autogen () {
	type autoconf >/dev/null 2>&1 || { echo >&2 "Cannot find \`autoconf'.  Aborting."; exit 1; }
	type automake >/dev/null 2>&1 || { echo >&2 "Cannot find \`automake'.  Aborting."; exit 1; }
	type autoreconf >/dev/null 2>&1 || { echo >&2 "Cannot find \`autoreconf'.  Aborting."; exit 1; }

	mkdir -p include/build-aux

	echo "running autoconf"; autoconf
	test -x configure || { echo >&2 "\`configure' was not generated.  Aborting."; exit 1; }

	# use automake only to copy files
	echo "copy needed files to include/build-aux"
	automake --add-missing --copy 2>/dev/null >/dev/null

	echo "running autoreconf on plugins/imagereader/libjpeg-turbo"
	autoreconf --install plugins/imagereader/libjpeg-turbo 2>/dev/null >/dev/null

	if [ ! -d plugins/ffms2/src ]; then
		type git >/dev/null 2>&1 || { echo >&2 "Cannot find \`git'.  Aborting."; exit 1; }
		echo "cloning ffms2 sources into plugins/ffms2/src"
		git clone -q --depth 1 "https://github.com/FFMS/ffms2.git" plugins/ffms2/src
		rm -rf ffms2/src/.git
	fi

	if [ ! -d plugins/lsmashsource/ffmpeg ]; then
		type git >/dev/null 2>&1 || { echo >&2 "Cannot find \`git'.  Aborting."; exit 1; }
		echo "cloning ffmpeg sources into plugins/lsmashsource/ffmpeg"
		git clone -q --depth 1 "git://source.ffmpeg.org/ffmpeg.git" plugins/lsmashsource/ffmpeg
		rm -rf plugins/lsmashsource/ffmpeg/.git
	fi
}

forcedelete="plugins/ffms2/src/ plugins/lsmashsource/ffmpeg/"

cleanscript="autogen-cleanup.sh"

if [ -f $cleanscript ]; then
	echo "warning: autogen clean-up script found!"
	echo "run \`./$cleanscript' first"
	exit 1
fi

find . > .autogen_sh_before
autogen
find . > .autogen_sh_after
diff -u .autogen_sh_before .autogen_sh_after | grep '^+\./' | sed 's|^+||g' | \
	grep -v '^\.\/git\/' | \
	grep -v '^\.\/hg\/' | \
	grep -v '^\.\/svn\/' | \
	grep -v '^\.\/bzr\/' | \
	grep -v '^\.\/\.autogen_sh_' > .autogen_sh_files

cat <<EOF> $cleanscript
#!/bin/sh
# this script was automatically generated by $(basename $0)
if [ -f Makefile ] || [ -f GNUmakefile ]; then
	(make maintainer-clean 2>/dev/null || 
	 make distclean 2>/dev/null || 
	 make clean 2>/dev/null) || true
fi
set -v
EOF

chmod a+x $cleanscript

for f in $(tac .autogen_sh_files); do
	if [ -d $f ]; then
		echo "rmdir $f 2>/dev/null || true" >> $cleanscript
	else
		echo "rm -f $f" >> $cleanscript
	fi
done
echo "rm -rf $forcedelete" >> $cleanscript
echo "rm -f $cleanscript" >> $cleanscript

rm -f .autogen_sh_before .autogen_sh_after .autogen_sh_files

