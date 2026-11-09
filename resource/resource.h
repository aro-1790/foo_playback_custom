#pragma once

// Control and dialog IDs of the configuration page. Kept in their own header so
// both the resource script and the C++ dialog class can include it.

#define IDD_CONFIG 3000

#ifndef IDC_STATIC
#define IDC_STATIC -1
#endif

// "Update when...?": the five timing choices, the first three sharing a row.
#define IDC_TIMING_DISABLE 1000
#define IDC_TIMING_START 1001
#define IDC_TIMING_END 1002
#define IDC_TIMING_PERCENT 1003
#define IDC_TIMING_SECONDS 1004

#define IDC_PERCENT_SLIDER 1010
#define IDC_PERCENT_TEXT 1011
#define IDC_SECONDS_EDIT 1012
// The two unit labels follow their control's mode, as in the original.
#define IDC_PERCENT_LABEL 1013
#define IDC_SECONDS_LABEL 1014

// Tags: whether each statistic is written, under which name, and the library
// filter.
#define IDC_WRITE_PLAY_COUNTER 1020
#define IDC_TAG_PLAY_COUNTER 1021
#define IDC_WRITE_FIRST_PLAYED 1022
#define IDC_TAG_FIRST_PLAYED 1023
#define IDC_WRITE_LAST_PLAYED 1024
#define IDC_TAG_LAST_PLAYED 1025
#define IDC_WRITE_PLAY_STAMP 1026
#define IDC_TAG_PLAY_STAMP 1027

// The media-library filter, at the bottom of the Tags group.
#define IDC_ONLY_LIBRARY 1040

// Storage: the three modes, consecutive so CheckRadioButton treats them as one
// group.
#define IDC_STORAGE_TAGS 1050
#define IDC_STORAGE_DATABASE 1051
#define IDC_STORAGE_FALLBACK 1052

// Prune drops the rows whose file is gone (and, with the checkbox, rows outside
// the library); Remove all empties the table. Neither is part of the radio group.
#define IDC_PRUNE_DATABASE 1053
#define IDC_PRUNE_OUTSIDE_LIBRARY 1054
#define IDC_CLEAR_DATABASE 1055

// Footnote: display field names bind at startup; the Properties group does not.
#define IDC_RESTART_NOTE 1056
