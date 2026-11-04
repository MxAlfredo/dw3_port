/*
 * Linker script fragment (INSERT: it adds output sections to the default script).
 *
 * Gathers the writable statics of every overlay the port links into one output section each, so
 * pc/boot.c can register them as state scopes (pc/shim/state_scope.h) without editing src/. Each
 * section ovl_<name>_data is delimited by __ovl_<name>_data_start / __ovl_<name>_data_end.
 * The field stages (WSTAG###) get the same treatment from a generated twin, build/ovl_stages.ld.
 *
 * Read-only data (.rodata*, .data.rel.ro*) is NOT matched by the section list on purpose.
 * pc/check_scopes.py parses this file and the generated one, finds the objects each statement
 * claims, and fails the build if a writable symbol or section of one of them lies outside its
 * range, if two statements claim one object, or if an object has a section the list misses.
 */
SECTIONS
{
    /* FIGHTSTG: battle mode (mode 0x6) */
    ovl_fightstg_data : ALIGN(64)
    {
        __ovl_fightstg_data_start = .;
        *build/battle/battle_mode/*.o(.data .data.* .sdata .sdata.* .bss .bss.* .sbss .sbss.* COMMON)
        . = ALIGN(64);
        __ovl_fightstg_data_end = .;
    }
    /* WFIGHTMN: battle menu sub-overlay, including the host menu.o replacement */
    ovl_wfightmn_data : ALIGN(64)
    {
        __ovl_wfightmn_data_start = .;
        *build/battle/normal_battle/*.o(.data .data.* .sdata .sdata.* .bss .bss.* .sbss .sbss.* COMMON)
        . = ALIGN(64);
        __ovl_wfightmn_data_end = .;
    }
    /* WFIGHTTS: battle test sub-overlay (the debug mode the port links) */
    ovl_wfightts_data : ALIGN(64)
    {
        __ovl_wfightts_data_start = .;
        *build/battle/battle_test.o(.data .data.* .sdata .sdata.* .bss .bss.* .sbss .sbss.* COMMON)
        . = ALIGN(64);
        __ovl_wfightts_data_end = .;
    }
    /* STFGTREP: battle report (mode 0x14) */
    ovl_stfgtrep_data : ALIGN(64)
    {
        __ovl_stfgtrep_data_start = .;
        *build/battle/report/*.o(.data .data.* .sdata .sdata.* .bss .bss.* .sbss .sbss.* COMMON)
        . = ALIGN(64);
        __ovl_stfgtrep_data_end = .;
    }
    /* FIELDSTG: the field (mode 0x2), src/field/field_mode and the host copies pc/field/*.c */
    ovl_fieldstg_data : ALIGN(64)
    {
        __ovl_fieldstg_data_start = .;
        *build/field/*.o(.data .data.* .sdata .sdata.* .bss .bss.* .sbss .sbss.* COMMON)
        . = ALIGN(64);
        __ovl_fieldstg_data_end = .;
    }
    /* STSTATUS: field menu (mode 0x10) */
    ovl_ststatus_data : ALIGN(64)
    {
        __ovl_ststatus_data_start = .;
        *build/fmenu/*.o(.data .data.* .sdata .sdata.* .bss .bss.* .sbss .sbss.* COMMON)
        . = ALIGN(64);
        __ovl_ststatus_data_end = .;
    }
    /* STGMCARD: memory card (mode 0xC) */
    ovl_stgmcard_data : ALIGN(64)
    {
        __ovl_stgmcard_data_start = .;
        *build/mcard/*.o(.data .data.* .sdata .sdata.* .bss .bss.* .sbss .sbss.* COMMON)
        . = ALIGN(64);
        __ovl_stgmcard_data_end = .;
    }
    /* STGDGLAB: Digimon Lab (mode 0xD) */
    ovl_stgdglab_data : ALIGN(64)
    {
        __ovl_stgdglab_data_start = .;
        *build/dlab/*.o(.data .data.* .sdata .sdata.* .bss .bss.* .sbss .sbss.* COMMON)
        . = ALIGN(64);
        __ovl_stgdglab_data_end = .;
    }
    /* STDGNAME: partner name (mode 0xB) */
    ovl_stdgname_data : ALIGN(64)
    {
        __ovl_stdgname_data_start = .;
        *build/pname/*.o(.data .data.* .sdata .sdata.* .bss .bss.* .sbss .sbss.* COMMON)
        . = ALIGN(64);
        __ovl_stdgname_data_end = .;
    }
    /* STGTRAIN: gym (mode 0xA) */
    ovl_stgtrain_data : ALIGN(64)
    {
        __ovl_stgtrain_data_start = .;
        *build/gym/*.o(.data .data.* .sdata .sdata.* .bss .bss.* .sbss .sbss.* COMMON)
        . = ALIGN(64);
        __ovl_stgtrain_data_end = .;
    }
    /* STITSHOP: item shop (mode 0xF) */
    ovl_stitshop_data : ALIGN(64)
    {
        __ovl_stitshop_data_start = .;
        *build/ishop/*.o(.data .data.* .sdata .sdata.* .bss .bss.* .sbss .sbss.* COMMON)
        . = ALIGN(64);
        __ovl_stitshop_data_end = .;
    }
    /* STCRDSHP: card shop (mode 0x13) */
    ovl_stcrdshp_data : ALIGN(64)
    {
        __ovl_stcrdshp_data_start = .;
        *build/cshop/*.o(.data .data.* .sdata .sdata.* .bss .bss.* .sbss .sbss.* COMMON)
        . = ALIGN(64);
        __ovl_stcrdshp_data_end = .;
    }
    /* STCRDDEK: deck editor (mode 0x4) */
    ovl_stcrddek_data : ALIGN(64)
    {
        __ovl_stcrddek_data_start = .;
        *build/deck/*.o(.data .data.* .sdata .sdata.* .bss .bss.* .sbss .sbss.* COMMON)
        . = ALIGN(64);
        __ovl_stcrddek_data_end = .;
    }
    /* STCRDABM: card album (mode 0x12) */
    ovl_stcrdabm_data : ALIGN(64)
    {
        __ovl_stcrdabm_data_start = .;
        *build/album/*.o(.data .data.* .sdata .sdata.* .bss .bss.* .sbss .sbss.* COMMON)
        . = ALIGN(64);
        __ovl_stcrdabm_data_end = .;
    }
    /* CARDGAME: card battle (mode 0x7); also CARDGAME_markedCounts of shim/host_overlay.c */
    ovl_cardgame_data : ALIGN(64)
    {
        __ovl_cardgame_data_start = .;
        *build/cgame/*.o(.data .data.* .sdata .sdata.* .bss .bss.* .sbss .sbss.* COMMON)
        *(.bss.ovl_cardgame)
        . = ALIGN(64);
        __ovl_cardgame_data_end = .;
    }
    /* STDWTITL: title, movies (mode 0xE); flat objects of $(BUILD), listed by name */
    ovl_stdwtitl_data : ALIGN(64)
    {
        __ovl_stdwtitl_data_start = .;
        *build/scene.o(.data .data.* .sdata .sdata.* .bss .bss.* .sbss .sbss.* COMMON)
        *build/movie.o(.data .data.* .sdata .sdata.* .bss .bss.* .sbss .sbss.* COMMON)
        *build/title_loader.o(.data .data.* .sdata .sdata.* .bss .bss.* .sbss .sbss.* COMMON)
        *build/title.o(.data .data.* .sdata .sdata.* .bss .bss.* .sbss .sbss.* COMMON)
        *build/background.o(.data .data.* .sdata .sdata.* .bss .bss.* .sbss .sbss.* COMMON)
        *build/logo.o(.data .data.* .sdata .sdata.* .bss .bss.* .sbss .sbss.* COMMON)
        *build/glint.o(.data .data.* .sdata .sdata.* .bss .bss.* .sbss .sbss.* COMMON)
        *build/slides.o(.data .data.* .sdata .sdata.* .bss .bss.* .sbss .sbss.* COMMON)
        *build/menu.o(.data .data.* .sdata .sdata.* .bss .bss.* .sbss .sbss.* COMMON)
        *build/edge_fade.o(.data .data.* .sdata .sdata.* .bss .bss.* .sbss .sbss.* COMMON)
        *build/splash.o(.data .data.* .sdata .sdata.* .bss .bss.* .sbss .sbss.* COMMON)
        *build/stdwtitl_data.o(.data .data.* .sdata .sdata.* .bss .bss.* .sbss .sbss.* COMMON)
        *build/title_data.o(.data .data.* .sdata .sdata.* .bss .bss.* .sbss .sbss.* COMMON)
        *build/movie_data.o(.data .data.* .sdata .sdata.* .bss .bss.* .sbss .sbss.* COMMON)
        . = ALIGN(64);
        __ovl_stdwtitl_data_end = .;
    }
    /* STPLNMET: player name entry (mode 0x5), objects named plnmet_* */
    ovl_stplnmet_data : ALIGN(64)
    {
        __ovl_stplnmet_data_start = .;
        *build/plnmet_*.o(.data .data.* .sdata .sdata.* .bss .bss.* .sbss .sbss.* COMMON)
        . = ALIGN(64);
        __ovl_stplnmet_data_end = .;
    }
    /* CNTY_SEL: country select (mode 0x16) */
    ovl_cntysel_data : ALIGN(64)
    {
        __ovl_cntysel_data_start = .;
        *build/country_select.o(.data .data.* .sdata .sdata.* .bss .bss.* .sbss .sbss.* COMMON)
        . = ALIGN(64);
        __ovl_cntysel_data_end = .;
    }
}
INSERT AFTER .data;
