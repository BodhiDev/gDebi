# -----------------------------------------------------------------------------
# gentoo main makefile
# -----------------------------------------------------------------------------

headers=buttons.h \
  buttonlayout.h \
  cfg_buttons.h \
  cfg_buttonlayout.h \
  cfg_cmdcfg.h \
  cfg_cmdseq.h \
  cfg_controls.h \
  cfg_dialogs.h \
  cfg_dirpane.h \
  cfg_errors.h\
  cfg_menus.h \
  cfg_module.h \
  cfg_nag.h \
  cfg_paths.h \
  cfg_styles.h \
  cfg_types.h \
  cfg_windows.h \
  children.h \
  cmd_about.h \
  cmd_activate.h \
  cmd_chmod.h \
  cmd_chown.h \
  cmd_configure.h \
  cmd_copy.h \
  cmd_copyas.h \
  cmd_delete.h \
  cmd_direnter.h \
  cmd_dirrescan.h \
  cmd_dpfocus.h \
  cmd_dpfocusisrch.h \
  cmd_dpmisc.h \
  cmd_file.h \
  cmd_generic.h \
  cmd_getsize.h \
  cmd_info.h \
  cmd_join.h \
  cmd_menupopup.h \
  cmd_mkdir.h \
  cmd_move.h \
  cmd_moveas.h \
  cmd_parent.h \
  cmd_quit.h \
  cmd_rename.h \
  cmd_renamere.h \
  cmd_renameseq.h \
  cmd_run.h \
  cmd_select.h \
  cmd_split.h \
  cmd_swap.h \
  cmd_symlink.h \
  cmd_viewtext.h \
  cmdarg.h \
  cmdgrab.h \
  cmdparse.h \
  cmdseq.h \
  cmdseq_config.h \
  cmdseq_dialog.h \
  color_dialog.h \
  configure.h \
  convstream.h \
  commands.h \
  controls.h \
  dialog.h \
  dirhistory.h \
  dirpane.h \
  dpformat.h \
  errors.h \
  events.h \
  file.h \
  fileutil.h \
  gfam.h \
  gentoo.h \
  gnu-gettext.h \
  guiutil.h \
  hash_dialog.h \
  icon_dialog.h \
  iconutil.h \
  list_dialog.h \
  keyboard.h \
  menus.h \
  mntent_wrap.h \
  miscutil.h \
  nag_dialog.h \
  odmultibutton.h \
  overwrite.h \
  progress.h \
  queue.h \
  sizeutil.h \
  strutil.h \
  style_dialog.h \
  styles.h \
  textview.h \
  types.h \
  userinfo.h \
  window.h \
  xmlutil.h

modules=buttons.c \
  buttonlayout.c \
  cfg_buttons.c \
  cfg_buttonlayout.c \
  cfg_cmdcfg.c \
  cfg_cmdseq.c \
  cfg_controls.c \
  cfg_dialogs.c \
  cfg_dirpane.c \
  cfg_errors.c\
  cfg_menus.c \
  cfg_nag.c \
  cfg_paths.c \
  cfg_styles.c \
  cfg_types.c \
  cfg_windows.c \
  children.c \
  cmd_about.c \
  cmd_activate.c \
  cmd_chmod.c \
  cmd_chown.c \
  cmd_configure.c \
  cmd_copy.c \
  cmd_copyas.c \
  cmd_delete.c \
  cmd_direnter.c \
  cmd_dirrescan.c \
  cmd_dpfocus.c \
  cmd_dpfocusisrch.c \
  cmd_dpmisc.c \
  cmd_file.c \
  cmd_generic.c \
  cmd_getsize.c \
  cmd_info.c \
  cmd_join.c \
  cmd_menupopup.c \
  cmd_mkdir.c \
  cmd_move.c \
  cmd_moveas.c \
  cmd_parent.c \
  cmd_quit.c \
  cmd_rename.c \
  cmd_renamere.c \
  cmd_renameseq.c \
  cmd_run.c \
  cmd_select.c \
  cmd_split.c \
  cmd_swap.c \
  cmd_symlink.c \
  cmd_viewtext.c \
  cmdarg.c \
  cmdgrab.c \
  cmdparse.c \
  cmdseq.c \
  cmdseq_config.c \
  cmdseq_dialog.c \
  color_dialog.c \
  configure.c \
  controls.c \
  convstream.c \
  dialog.c \
  dirhistory.c \
  dirpane.c \
  dpformat.c \
  errors.c \
  events.c \
  file.c \
  fileutil.c \
  gfam.c \
  gentoo.c \
  guiutil.c \
  hash_dialog.c \
  icon_dialog.c \
  iconutil.c \
  keyboard.c \
  list_dialog.c \
  menus.c \
  miscutil.c \
  mntent_wrap.c \
  nag_dialog.c \
  odmultibutton.c \
  overwrite.c \
  progress.c \
  queue.c \
  sizeutil.c \
  strutil.c \
  style_dialog.c \
  styles.c \
  textview.c \
  types.c \
  userinfo.c \
  window.c \
  xmlutil.c

# This is the compiled-in graphics. It's mostly default icons,
# but also the logo for the About window.
graphics=graphics

# Targets ---------------------------------------------------------------------

localedir=@localedir@

AM_CFLAGS=@GENTOO_CFLAGS@ @GTK_CFLAGS@ -I../widgets \
  -DPATH_CFG=\"$(sysconfdir)\" \
  -DPATH_GRC=\"$$HOME/\" \
  -DPATH_ICN=\"$(datadir)/gentoo/icons\"\
  -DLOCALEDIR=\"$(localedir)\"

bin_PROGRAMS=gentoo
gentoo_SOURCES=$(headers) $(modules) $(graphics)
gentoo_LDADD=@GTK_LIBS@ @MODULES_LIBS@
