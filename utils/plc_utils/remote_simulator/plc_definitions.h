#define PLC_TIMEOUT_MS     20
#define PLC_RETRIES        20
#define PLC_STAT_REQ       "@00RD0150001754*\r"
#define PLC_STAT_REQ_LEN   17
#define PLC_STAT_RESP_LEN  79
#define PLC_CMD_HEAD       "@00WD0100"
#define PLC_CMD_LEN        57
#define PLC_CMD_HEAD_LEN   9
#define PLC_CMD_RESP       "@00WD0053*\r"
#define PLC_CMD_RESP_LEN   11

// Offsets of relevant info in PLC status response
#define STAT_ENDC_OFFS           5  /* String */
#define STAT_DOME_POS_OFFS       7  /* String */
#define STAT_DROPOUT_OFFS       12
#define STAT_SHUTTER_OFFS       13
#define STAT_DOME_STAT_OFFS     14
#define STAT_APER_STAT_OFFS     15
#define STAT_FILT_STAT_OFFS     16
#define STAT_ACQMIR_OFFS        17
#define STAT_INSTR_SHUTT_OFFS   18
#define STAT_FOCUS_REG_OFFS     19  /* '8' == IN (+), '0' == OUT (-)  */
#define STAT_FOCUS_POS_OFFS     20  /* String  */
#define STAT_FOC_STAT_OFFS1     25
#define STAT_FOC_STAT_OFFS2     26
#define STAT_CRIT_ERR_OFFS      27
#define STAT_EHT_OFFS           28
#define STAT_HANDSET_OFFS1      29
#define STAT_HANDSET_OFFS2      30
#define STAT_APER_NUM_OFFS      32
#define STAT_FILT_NUM_OFFS      34
#define STAT_DOME_OFFS_OFFS     35  /* String */
#define STAT_DOME_MAX_FLOP_OFFS 39  /* String */
#define STAT_DOME_MIN_FLOP_OFFS 41  /* String */
#define STAT_TEL_RA_LO_OFFS     43  /* String */
#define STAT_TEL_RA_HI_OFFS     47  /* String */
#define STAT_TEL_DEC_LO_OFFS    51  /* String */
#define STAT_TEL_DEC_HI_OFFS    55  /* String */
#define STAT_FCS_OFFS           75  /* String */

// PLC Status Response Masks
#define STAT_HEADER_FMT            "%02X"
#define STAT_DOME_POS_FMT          "%04hu"
#define STAT_DOME_POS_LEN          4
#define STAT_DSHUTT_OPEN_MASK      0x1   /* Also means dropout is open in DOME_DROPOUT half-byte */
#define STAT_DSHUTT_CLOSED_MASK    0x2   /* Also: dropout open in DOME_DROPOUT */
#define STAT_DSHUTT_MOVING_MASK    0x4   /* Also: dropout moving in DOME_DROPOUT */
#define STAT_TRAPDOOR_OPEN_MASK    0x1
#define STAT_DOME_MOVING_MASK      0x2
#define STAT_APER_INIT_MASK        0x1   /* Also: filter init in FILT_STAT */
#define STAT_APER_CENT_MASK        0x2   /* Also: filter cent in FILT_STAT */
#define STAT_APER_MOVING_MASK      0x4   /* Also: filter moving in FILT_STAT */
#define STAT_ACQMIR_VIEW_MASK      0x1
#define STAT_ACQMIR_MEAS_MASK      0x2
#define STAT_ACQMIR_MOVING_MASK    0x4
#define STAT_INSTR_SHUTT_OPEN_MASK 0x1
#define STAT_FOCUS_REG_OUT_VAL     '8'
#define STAT_FOCUS_REG_IN_VAL      '0'
#define STAT_FOCUS_POS_FMT         "%03hu"
#define STAT_FOCUS_POS_LEN         4
#define STAT_FOC_SLOT_MASK         0x1
#define STAT_FOC_REF_MASK          0x2
#define STAT_FOC_OUT_MASK          0x4
#define STAT_FOC_IN_MASK           0x8
#define STAT_FOC_MOVING_MASK       0x1
#define STAT_FOC_INIT_MASK         0x2
#define STAT_FOC_STALL_MASK        0x4
#define STAT_POWER_FAIL_MASK       0x4
#define STAT_WATCHDOG_MASK         0x8
#define STAT_EHT_MAN_OFF_MASK      0x1
#define STAT_EHT_LO_MASK           0x2
#define STAT_EHT_HI_MASK           0x4
#define STAT_HANDSET_FOC_IN_MASK   0x1
#define STAT_HANDSET_FOC_OUT_MASK  0x2
#define STAT_HANDSET_SLEW_MASK     0x4
#define STAT_HANDSET_GUIDE_MASK    0x8
#define STAT_HANDSET_SOUTH_MASK    0x1
#define STAT_HANDSET_NORTH_MASK    0x2
#define STAT_HANDSET_EAST_MASK     0x4
#define STAT_HANDSET_WEST_MASK     0x8
#define STAT_APER_NUM_FMT          "%1hu"
#define STAT_FILT_NUM_FMT          "%1hu"
#define STAT_DOME_OFFS_FMT         "%04hu"
#define STAT_DOME_FLOP_FMT         "%02hu"
#define STAT_DOME_FLOP_LEN         2

// Offsets of relevant info in PLC control string
#define CNTR_DOME_POS_OFFS       9
#define CNTR_DOME_MAX_FLOP_OFFS 13
#define CNTR_DOME_MIN_FLOP_OFFS 15
#define CNTR_INSTR_SHUTT_OFFS   17
#define CNTR_DROPOUT_OFFS       18
#define CNTR_SHUTTER_OFFS       19
#define CNTR_DOME_STAT_OFFS     20
#define CNTR_FOCUS_OFFS         21
#define CNTR_APER_STAT_OFFS     22
#define CNTR_FILT_STAT_OFFS     23
#define CNTR_ACQMIR_EHT_OFFS    24
#define CNTR_FILT_NUM_OFFS      25
#define CNTR_APER_NUM_OFFS      27
#define CNTR_FOCUS_REG_OFFS     29
#define CNTR_FOCUS_POS_OFFS     30
#define CNTR_DOME_OFFS_OFFS     33
#define CNTR_PADDING_OFFS       37
#define CNTR_FCS_TERM_OFFS      53

// PLC Control Structure Masks
#define CNTR_DOME_POS_FMT           "%04hu"
#define CNTR_DOME_POS_LEN          4
#define CNTR_DOME_FLOP_FMT          "%02hu"
#define CNTR_DOME_FLOP_LEN         2
#define CNTR_INSTR_SHUTT_MASK      0x1
#define CNTR_ACQRESET_MASK         0x2   /* Extra bit in INSTR_SHUTT half-byte*/
#define CNTR_WATCHDOG_MASK         0x8   /* Extra bit (INSTR_SHUTT) */
#define CNTR_DSHUTT_OPEN_MASK      0x1   /* Also to open dropout in DOME_DROPOUT half-byte */
#define CNTR_DSHUTT_CLOSE_MASK     0x2   /* Also: close dropout in DOME_DROPOUT */
#define CNTR_LIM_S_MASK            0x4   /* Extra bit (DOME_DROPOUT) */
#define CNTR_LIM_W_MASK            0x8   /* Extra bit (DOME_DROPOUT) */
#define CNTR_DOME_GUIDE_MASK       0x1
#define CNTR_DOME_MOVE_LEFT_MASK   0x2
#define CNTR_DOME_MOVE_MASK        0x4
#define CNTR_DOME_SET_OFFS_MASK    0x8
#define CNTR_FOC_GO_MASK           0x1
#define CNTR_FOC_OUT_MASK          0x2
#define CNTR_FOC_IN_MASK           0x4
#define CNTR_FOC_RESET_MASK        0x8
#define CNTR_FOC_INIT_MASK         0x8   /* Extra bit (APER_STAT) */
#define CNTR_APER_INIT_MASK        0x1   /* Also: init filter in FILT_STAT */
#define CNTR_APER_GO_MASK          0x2   /* Also: go filter in FILT_STAT */
#define CNTR_APER_RESET_MASK       0x4   /* Also: reset filter in FILT_STAT */
#define CNTR_EHT_LO_MASK           0x1
#define CNTR_EHT_HI_MASK           0x2
#define CNTR_ACQMIR_RESET_MASK     0x4
#define CNTR_ACQMIR_INBEAM_MASK    0x8
#define CNTR_FILT_NUM_FMT          "%02hu"
#define CNTR_APER_NUM_FMT          "%02hu"
#define CNTR_FOCUS_REG_OUT_VAL     '8'
#define CNTR_FOCUS_REG_IN_VAL      '0'
#define CNTR_FOCUS_POS_FMT         "%03hu"
#define CNTR_FOCUS_POS_LEN         3
#define CNTR_DOME_OFFS_FMT         "%04hu"
#define CNTR_PADDING_FMT           "%016u"
#define CNTR_PADDING_LEN           16
#define CNTR_FCS_TERM_FMT          "%02X*\r"
#define CNTR_FCS_TERM_LEN          4

