/*
 * irx_clang.h - ps2sdk's import tables for clang.
 *
 * irx.h's DECLARE_IMPORT_TABLE is a C variable followed by file-scope asm for
 * the stubs, which needs gcc's -fno-toplevel-reorder to stay in that order.
 * clang has no such option and puts file-scope asm first, so here the table's
 * header is file-scope asm as well. Force-included when compiling imports.lst.
 */

#ifndef IOP_IRX_CLANG_H
#define IOP_IRX_CLANG_H

#include "irx.h"

#define IRX_CLANG_STR(val) STR(val)

/* struct irx_import_table up to its stubs: magic, next, version, mode, name */
#undef DECLARE_IMPORT_TABLE
#define DECLARE_IMPORT_TABLE(modname, major, minor) \
	__asm__ (".section\t.text\n\t"		\
		".balign 4\n\t"			\
		".word " IRX_CLANG_STR(IMPORT_MAGIC) ", 0\n\t"	\
		".short " IRX_CLANG_STR(IRX_VER(major, minor)) ", 0\n\t"	\
		"9:\n\t.ascii \"" #modname "\"\n\t"	\
		".space 8 - (. - 9b)");

#endif
