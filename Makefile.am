# -----------------------------------------------------------------------------
# gentoo root Makefile.am
# -----------------------------------------------------------------------------

EXTRA_DIST=config.rpath BUGS CONFIG-CHANGES CREDITS README.NLS README.gtkrc docs icons gentoo.spec
SUBDIRS=m4 icons po src

sysconf_DATA=gentoorc gentoogtkrc

# Get my personal config, and massage it into a gentoorc.in template. This should
# make things better. It abstracts my config by replacing hard-coded stuff with
# autoconf symbols and other generics. This should be run prior to "make dist".
config:
	cp gentoorc.in gentoorc.in.bak
	sed -e 's/<version>\"[0-9.a-z]*\"<\/version>/<version>"\@VERSION\@"<\/version>/' \
	    -e 's/<path>\"icons\"<\/path>/<path>\"\@prefix\@\/share\/\@PACKAGE\@\/icons\/\"<\/path>/' \
	    -e 's/<defpath>\"[-0-9.a-zA-Z_~/]*\"<\/defpath>/<defpath>"~"<\/defpath>/' \
	    -e 's/<w>\([8-9][0-9]*\)/<w>800/' \
	    -e 's/<h>\([6-9][0-9]*\)/<h>600/' \
	    -e 's/<ignore>\("[a-z-]*"\)<\/ignore>//' \
	    < ~/.config/gentoo/gentoorc >gentoorc.in

ACLOCAL_AMFLAGS=-I m4

#
# Remove any Subversion files, since we don't want those to be distributed.
#
dist-hook:
	find $(top_distdir) -name .svn -type d | xargs rm -rf
