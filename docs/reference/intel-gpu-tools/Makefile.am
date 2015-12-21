## Process this file with automake to produce Makefile.in

TESTLISTS = $(top_builddir)/tests/test-list.txt
KEYWORDS = (invalid|hang|swap|thrash|crc|tiled|tiling|rte|ctx|render|blt|bsd|vebox|exec|rpm)

xml/igt_test_programs_%_programs.xml: $(TESTLISTS)
	mkdir -p `dirname $@`
	echo "<?xml version=\"1.0\"?>" > $@
	echo "<!DOCTYPE refsect1 PUBLIC \"-//OASIS//DTD DocBook XML V4.3//EN\"" >> $@
	echo "               \"http://www.oasis-open.org/docbook/xml/4.3/docbookx.dtd\"" >> $@
	echo "[" >> $@
	echo "  <!ENTITY % local.common.attrib \"xmlns:xi  CDATA  #FIXED 'http://www.w3.org/2003/XInclude'\">" >> $@
	echo "  <!ENTITY version SYSTEM \"version.xml\">" >> $@
	echo "]>" >> $@
	echo "<refsect1>" >> $@
	echo "<title>Programs</title>" >> $@
	echo "<informaltable pgwide=\"1\" frame=\"none\"><tgroup cols=\"2\"><tbody>" >> $@
	for test in `cat $(TESTLISTS) | tr ' ' '\n' | grep "^$*" | sort`; do \
		echo "<row><entry role=\"program_name\">" >> $@; \
		echo "<link linkend=\"$$test\">$$test</link></entry></row>" >> $@; \
	done;
	echo "</tbody></tgroup></informaltable>" >> $@
	echo "</refsect1>" >> $@

xml/igt_test_programs_%_description.xml: $(TESTLISTS)
	mkdir -p `dirname $@`
	echo "<?xml version=\"1.0\"?>" > $@
	echo "<!DOCTYPE refsect1 PUBLIC \"-//OASIS//DTD DocBook XML V4.3//EN\"" >> $@
	echo "               \"http://www.oasis-open.org/docbook/xml/4.3/docbookx.dtd\"" >> $@
	echo "[" >> $@
	echo "  <!ENTITY % local.common.attrib \"xmlns:xi  CDATA  #FIXED 'http://www.w3.org/2003/XInclude'\">" >> $@
	echo "  <!ENTITY version SYSTEM \"version.xml\">" >> $@
	echo "]>" >> $@
	echo "<refsect1>" >> $@
	echo "<title>Description</title>" >> $@
	for test in `cat $(TESTLISTS) | tr ' ' '\n' | grep "^$*" | sort`; do \
		echo "<refsect2 id=\"$$test\"><title>" >> $@; \
		echo "$$test" | perl -pe 's/(?<=_)$(KEYWORDS)(?=(_|\W))/<acronym>\1<\/acronym>/g' >> $@; \
		echo "</title><para><![CDATA[" >> $@; \
		if [ -x $(top_builddir)/tests/$$test ]; then \
			testprog=$(top_builddir)/tests/$$test; \
		else \
			testprog=$(top_srcdir)/tests/$$test; \
		fi; \
		 ./$$testprog --help-description >> $@; \
		echo "]]></para>" >> $@; \
		if ./$$testprog --list-subtests > /dev/null ; then \
			echo "<refsect3><title>Subtests</title>" >> $@; \
			subtest_list=`./$$testprog --list-subtests`; \
			subtest_count=`echo $$subtest_list | wc -w`; \
			if [ $$subtest_count -gt 100 ]; then \
				echo "<para>This test has over 100 subtests. " >> $@; \
				echo "Run <command>$$test</command> <option>--list-subtests</option> to list them.</para>" >> $@; \
			else \
				echo "<simplelist>" >> $@; \
				for subtest in $$subtest_list; do \
					echo "<member>" >> $@; \
					echo "$$subtest" | perl -pe 's/\b$(KEYWORDS)\b/<acronym>\1<\/acronym>/g' >> $@; \
					echo "</member>" >> $@; \
				done; \
				echo "</simplelist>" >> $@; \
			fi; \
			echo "</refsect3>" >> $@; \
		fi; \
		echo "</refsect2>" >> $@; \
	done;
	echo "</refsect1>" >> $@

# We require automake 1.6 at least.
AUTOMAKE_OPTIONS = 1.6

# This is a blank Makefile.am for using gtk-doc.
# Copy this to your project's API docs directory and modify the variables to
# suit your project. See the GTK+ Makefiles in gtk+/docs/reference for examples
# of using the various options.

# The name of the module, e.g. 'glib'.
DOC_MODULE=intel-gpu-tools

# Uncomment for versioned docs and specify the version of the module, e.g. '2'.
#DOC_MODULE_VERSION=2


# The top-level XML file (SGML in the past). You can change this if you want to.
DOC_MAIN_SGML_FILE=$(DOC_MODULE)-docs.xml

# Directories containing the source code.
# gtk-doc will search all .c and .h files beneath these paths
# for inline comments documenting functions and macros.
# e.g. DOC_SOURCE_DIR=$(top_srcdir)/gtk $(top_srcdir)/gdk
DOC_SOURCE_DIR=$(top_srcdir)/lib

# Extra options to pass to gtkdoc-scangobj. Not normally needed.
SCANGOBJ_OPTIONS=

# Extra options to supply to gtkdoc-scan.
# e.g. SCAN_OPTIONS=--deprecated-guards="GTK_DISABLE_DEPRECATED"
SCAN_OPTIONS=--rebuild-sections

# Extra options to supply to gtkdoc-mkdb.
# e.g. MKDB_OPTIONS=--xml-mode --output-format=xml
MKDB_OPTIONS=--output-format=xml

# Extra options to supply to gtkdoc-mktmpl
# e.g. MKTMPL_OPTIONS=--only-section-tmpl
MKTMPL_OPTIONS=

# Extra options to supply to gtkdoc-mkhtml
MKHTML_OPTIONS=

# Extra options to supply to gtkdoc-fixref. Not normally needed.
# e.g. FIXXREF_OPTIONS=--extra-dir=../gdk-pixbuf/html --extra-dir=../gdk/html
FIXXREF_OPTIONS=

# Used for dependencies. The docs will be rebuilt if any of these change.
# e.g. HFILE_GLOB=$(top_srcdir)/gtk/*.h
# e.g. CFILE_GLOB=$(top_srcdir)/gtk/*.c
HFILE_GLOB=$(top_srcdir)/lib/*.h
CFILE_GLOB=$(top_srcdir)/lib/*.c

# Extra header to include when scanning, which are not under DOC_SOURCE_DIR
# e.g. EXTRA_HFILES=$(top_srcdir}/contrib/extra.h
EXTRA_HFILES=

# Header files or dirs to ignore when scanning. Use base file/dir names
# e.g. IGNORE_HFILES=gtkdebug.h gtkintl.h private_code
IGNORE_HFILES=gen6_render.h gen7_media.h gen7_render.h gen8_media.h \
	      gen8_render.h gpgpu_fill.h i830_reg.h i915_3d.h i915_pciids.h \
	      i915_reg.h igt_edid_template.h intel_reg.h debug.h instdone.h \
	      media_fill.h rendercopy.h media_spin.h media_fill_gen9.h \
	      gen9_render.h version.h

# Images to copy into HTML directory.
# e.g. HTML_IMAGES=$(top_srcdir)/gtk/stock-icons/stock_about_24.png
HTML_IMAGES=

test_program_files = xml/igt_test_programs_core_description.xml \
		     xml/igt_test_programs_core_programs.xml \
		     xml/igt_test_programs_debugfs_description.xml \
		     xml/igt_test_programs_debugfs_programs.xml \
		     xml/igt_test_programs_drm_description.xml \
		     xml/igt_test_programs_drm_programs.xml \
		     xml/igt_test_programs_drv_description.xml \
		     xml/igt_test_programs_drv_programs.xml \
		     xml/igt_test_programs_gem_description.xml \
		     xml/igt_test_programs_gem_programs.xml \
		     xml/igt_test_programs_gen3_description.xml \
		     xml/igt_test_programs_gen3_programs.xml \
		     xml/igt_test_programs_kms_description.xml \
		     xml/igt_test_programs_kms_programs.xml \
		     xml/igt_test_programs_pm_description.xml \
		     xml/igt_test_programs_pm_programs.xml \
		     xml/igt_test_programs_prime_description.xml \
		     xml/igt_test_programs_prime_programs.xml \
		     xml/igt_test_programs_sysfs_description.xml \
		     xml/igt_test_programs_sysfs_programs.xml \
		     $(NULL)

# Extra SGML files that are included by $(DOC_MAIN_SGML_FILE).
# e.g. content_files=running.sgml building.sgml changes-2.0.sgml
content_files=igt_test_programs.xml $(test_program_files) \
	      $(NULL)

# SGML files where gtk-doc abbrevations (#GtkWidget) are expanded
# These files must be listed here *and* in content_files
# e.g. expand_content_files=running.sgml
expand_content_files=igt_test_programs.xml

# CFLAGS and LDFLAGS for compiling gtkdoc-scangobj with your library.
# Only needed if you are using gtkdoc-scangobj to dynamically query widget
# signals and properties.
# e.g. GTKDOC_CFLAGS=-I$(top_srcdir) -I$(top_builddir) $(GTK_DEBUG_FLAGS)
# e.g. GTKDOC_LIBS=$(top_builddir)/gtk/$(gtktargetlib)
GTKDOC_CFLAGS=
GTKDOC_LIBS=

# This includes the standard gtk-doc make rules, copied by gtkdocize.
include $(top_srcdir)/gtk-doc.make

# Other files to distribute
# e.g. EXTRA_DIST += version.xml.in
# EXTRA_DIST +=

# Files not to distribute
# for --rebuild-types in $(SCAN_OPTIONS), e.g. $(DOC_MODULE).types
# for --rebuild-sections in $(SCAN_OPTIONS) e.g. $(DOC_MODULE)-sections.txt
DISTCLEANFILES = $(DOC_MODULE)-sections.txt $(test_program_files)

CLEANFILES += $(test_program_files)

# Comment this out if you want 'make check' to test you doc status
# and run some sanity checks
if ENABLE_GTK_DOC
TESTS_ENVIRONMENT = cd $(srcdir) && \
  DOC_MODULE=$(DOC_MODULE) DOC_MAIN_SGML_FILE=$(DOC_MAIN_SGML_FILE) \
  SRCDIR=$(abs_srcdir) BUILDDIR=$(abs_builddir)
#TESTS = $(GTKDOC_CHECK)
endif

-include $(top_srcdir)/git.mk
