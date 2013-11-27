/*!
* \addtogroup common
* \file act_ipc.h
* \brief Header file for IPC spec
*
* Contains defines, enums and structs necessary to effect efficient communication between components of
* the ACT control software suite.
* 
* The ACT control software suite's beauty lies in its modularity, which rests firmly on the definitions
* in this file. In principle, this file defines only the message structures that must be passed 
* between programmes of the software suite and the definitions for interpreting the fields of these
* structures.
* 
* Broadly speaking, there are three groups of message structure types:
* - Control messages, which communicate control information between the controller and each programme
*   it manages individually.
* - Service messages, which relay information such as the time, telescope coordinates and situational
*   parameters between programmes that provide and/or require this information.
* - Observation messages, which relay information about a particular observation between programmes
*   that have a part to play during observations.
* 
* When modifying this file, the following advice is given:
* - Don't modify existing definitions, you will also need to modify each programme that uses the 
*   modified definition and check for inconsistensies in the whole software suite.
* - New definitions may be added at will, although it is recommended that the whole software suite
*   be recompiled - note any errors or warnings, they are likely significant.
* - Unused definitions may be removed at will, although it is also recommended that the whole
*   software suite be recompiled after doing so. Again, note any errors.
* 
* TODO:
*   Add new stage for observing cycles at the end that links back to scheduler and let scheduler
*   cycle back to first stage (instead of controller). This is so the controller won't automatically
*   cycle back to first stage and cause an infinite loop in case the scheduler is missing when the
*    last stage is completed.
*/

#ifndef ACT_IPC_H
#define ACT_IPC_H

#include "act_timecoord.h"

/*! \name Service types
 * \brief Definitions of services that programmes can provide/require.
 *
 * As an example, a programme might need to know what the time is but lack the ability to determine
 * the time. Another programme may be able to determine the time and disseminate that information.
 * During startup then, the service client (the programme that requires the time) reports that it
 * requires the time and the service providor (the programme that can determine the time) reports
 * that it can provide time information.
 *
 * Defines may be added to this list without the need to recompile any of the software components that do
 * not use the new define, provided that the define is given an unused number. If any of the existing
 * defines are modified, all software components that make use of the modified define.
 \{ */
#define SERVICE_TIME      0x01  /**< Local time.*/
#define SERVICE_COORD     0x02  /**< Telescope coordinates.*/
#define SERVICE_ENVIRON   0x04  /**< Environment.*/
/*! \} */

/*! \name Control defininitions
 * \brief Definitions to communicate control information between the controller and its subprogrammes.
 */
/*! \{ */
enum
{
  PROGSTAT_STARTUP = 1,      /**< Programme is starting up. */
  PROGSTAT_RUNNING,          /**< Programme is running. */
  PROGSTAT_STOPPING,         /**< Programme is shutting down. */
  PROGSTAT_STOPPED,          /**< Programme is not running. */
  PROGSTAT_KILLED,           /**< Programme has been forcibly closed. */
  PROGSTAT_ERR_RESTART,      /**< Programme encountered an error and should be restarted. */
  PROGSTAT_ERR_CRIT          /**< Programme encountered a critical error, stop everything and await assistance. */
};

/*! \name Active time definitions.
 * \brief Definitions of possible states of operation of the software.
 * 
 * A programme can be active during the day, during the night, neither night nor day, or both night
 * and day. Each subprogramme will be started and stopped by the main controller according to the
 * active times it specifies to the main controller. 
 * \{ */
#define ACTIVE_TIME_DAY   0x01  /**< Software in IDLE mode (daytime).*/
#define ACTIVE_TIME_NIGHT 0x02  /**< Software in ACTIVE mode (night time).*/
/*! \} */

/*! \name Target set stage definitions
 * \brief Target set cycle is enforced using a flag in the MT_TARG_SET message structure by these defines.
 * 
 * The sequence of operations that must be followed when a target needs to be set is given by these
 * definitions. Each one (or more) programme registers itself as being able to perform each one (or
 * more) of the steps defined here. When a programme receives an MT_TARG_SET message where the step
 * (given by the 'stage' field of the MT_TARG_SET structure) is the step (or one of the steps) it
 * performs, it performs its duties for that step and then returns the message.
 * 
 * Since the controller is the central hub of communication in the software suite, it awaits an
 * MT_TARG_SET message from the programme performing the first step defined here before any part
 * of an observation can be started. When the controller receives an MT_TARG_SET message, it
 * increases the 'stage' flag by one bitmask position and sends the message to the programme(s)
 * performing the corresponding step. When the controller receives a message with the stage is
 * equal to TARGSET_NONE, it cycles it around to the first stage defined here.
 */
/*! \{ */
#define TARGSET_SCHED_PRE   0x01  /**< Select target.*/
#define TARGSET_ENVIRON     0x02  /**< Check environmental and situational parameters.*/
#define TARGSET_MOVE_TEL    0x04  /**< Move telescope to target.*/
#define TARGSET_ACQUIRE     0x08  /**< Check telescope position and acquire target (auto mode only).*/
#define TARGSET_SCHED_POST  0x10  /**< Return message to scheduler*/
#define TARGSET_NONE        0x20  /**< Invalid stage*/
/*! \} */

/*! \name Data collection with PMT stage definitions
 * \brief PMT data cycle is enforced using a flag in the MT_DATA_PMT message structure by these defines.
 * 
 * The sequence of operations that must be followed when a target needs to be set is given by these
 * definitions. Each one (or more) programme registers itself as being able to perform each one (or
 * more) of the steps defined here. When a programme receives an MT_DATA_PMT message where the step
 * (given by the 'stage' field of the MT_DATA_PMT structure) is the step (or one of the steps) it
 * performs, it performs its duties for that step and then returns the message.
 * 
 * Since the controller is the central hub of communication in the software suite, it awaits an
 * MT_DATA_PMT message from the programme performing the first step defined here before any part
 * of an observation can be started. When the controller receives an MT_DATA_PMT message, it
 * increases the 'stage' flag by one bitmask position and sends the message to the programme(s)
 * performing the corresponding step. When the controller receives a message with the stage is
 * equal to DATAPMT_NONE, it cycles it around to the first stage defined here.
 */
/*! \{ */
#define DATAPMT_SCHED_PRE   0x01  /**< Select observational parameters for PMT.*/
#define DATAPMT_ENVIRON     0x02  /**< Check environmental and situational parameters.*/
#define DATAPMT_PREP_PHOTOM 0x04  /**< Prepare to collect data with PMT.*/
#define DATAPMT_PHOTOM      0x08  /**< Collect data with PMT.*/
#define DATAPMT_SCHED_POST  0x10  /**< Return message to scheduler.*/
#define DATAPMT_NONE        0x20  /**< Invalid stage.*/
/*! \} */

/*! \name Data collection with CCD stage definitions
 * \brief CCD data cycle is enforced using a flag in the MT_DATA_CCD message structure by these defines.
 * 
 * The sequence of operations that must be followed when a target needs to be set is given by these
 * definitions. Each one (or more) programme registers itself as being able to perform each one (or
 * more) of the steps defined here. When a programme receives an MT_DATA_CCD message where the step
 * (given by the 'stage' field of the MT_DATA_CCD structure) is the step (or one of the steps) it
 * performs, it performs its duties for that step and then returns the message.
 * 
 * Since the controller is the central hub of communication in the software suite, it awaits an
 * MT_DATA_CCD message from the programme performing the first step defined here before any part
 * of an observation can be started. When the controller receives an MT_DATA_CCD message, it
 * increases the 'stage' flag by one bitmask position and sends the message to the programme(s)
 * performing the corresponding step. When the controller receives a message with the stage is
 * equal to DATACCD_NONE, it cycles it around to the first stage defined here.
 */
/*! \{ */
#define DATACCD_SCHED_PRE   0x01  /**< Select observational parameters for CCD.*/
#define DATACCD_ENVIRON     0x02  /**< Check environmental and situational parameters.*/
#define DATACCD_PREP_PHOTOM 0x04  /**< Prepare to collect data with CCD.*/
#define DATACCD_PHOTOM      0x08  /**< Collect data with CCD.*/
#define DATACCD_SCHED_POST  0x10  /**< Return message to scheduler.*/
#define DATACCD_NONE        0x20  /**< Past last stage of target set cycle, go back to beginning.*/
/*! \} */

/*! \name Observation status definitions.
 * \brief Status of observation, used mostly to report errors and severity of errors.
 *
 * Used in MT_TARG_SET, MT_DATA_CCD and MT_DATA_PMT.
 */
/*! \{ */
enum
{
  OBSNSTAT_GOOD = 1,  /**< Observation is proceeding normally. */
  OBSNSTAT_CANCEL,    /**< Observation was cancelled. */
  OBSNSTAT_COMPLETE,  /**< Observation complete successfully. */
  OBSNSTAT_ERR_RETRY, /**< An error occurred, try again. */
  OBSNSTAT_ERR_WAIT,  /**< The observation cannot currently be done, try again later. */
  OBSNSTAT_ERR_NEXT,  /**< An error occurred, try next observation (target). */
  OBSNSTAT_ERR_CRIT   /**< An error occurred, stop everything and await assistance. */
};
/*! \} */

/// Maximum length of programme version number string.
#define MAX_VERSION_LEN   20

/// Maximum length of target identifer string.
#define MAX_TARGID_LEN    20

/// Maximum length of X display number string.
#define MAX_XDISPID_LEN   30

/** \name Instrument capabilities definitions.
 * \{
 */
/// Definition of maximum length of instrument identifier string
#define IPC_MAX_INSTRID_LEN         20

/// Maximum number of filters (and apertures) that the software supports.
#define IPC_MAX_NUM_FILTAPERS       10

/// Maximum length of each filter (aperture) identifier string.
#define IPC_MAX_FILTAPER_NAME_LEN    5

/// PMT integrations available
#define IPC_PMT_MODE_INTEGRATE     0x01

/// PMT photon tagging available
#define IPC_PMT_MODE_PHOTTAG       0x02

/// Maximum number of CCD window modes supported
#define IPC_CCD_MAX_NUM_WINDOW_MODES  1
/** \} */

/** \brief Prebinning modes supported by the CCD/driver
 * \{ */
#define IPC_IMG_PREBIN_1   0x00000001
#define IPC_IMG_PREBIN_2   0x00000002
#define IPC_IMG_PREBIN_3   0x00000004
#define IPC_IMG_PREBIN_4   0x00000008
#define IPC_IMG_PREBIN_5   0x00000010
#define IPC_IMG_PREBIN_6   0x00000020
#define IPC_IMG_PREBIN_7   0x00000040
#define IPC_IMG_PREBIN_8   0x00000080
#define IPC_IMG_PREBIN_9   0x00000100
#define IPC_IMG_PREBIN_10  0x00000200
#define IPC_IMG_PREBIN_11  0x00000400
#define IPC_IMG_PREBIN_12  0x00000800
#define IPC_IMG_PREBIN_13  0x00001000
#define IPC_IMG_PREBIN_14  0x00002000
#define IPC_IMG_PREBIN_15  0x00004000
#define IPC_IMG_PREBIN_16  0x00008000
#define IPC_IMG_PREBIN_17  0x00010000
#define IPC_IMG_PREBIN_18  0x00020000
#define IPC_IMG_PREBIN_19  0x00040000
#define IPC_IMG_PREBIN_20  0x00080000
#define IPC_IMG_PREBIN_21  0x00100000
#define IPC_IMG_PREBIN_22  0x00200000
#define IPC_IMG_PREBIN_23  0x00400000
#define IPC_IMG_PREBIN_24  0x00800000
#define IPC_IMG_PREBIN_25  0x01000000
#define IPC_IMG_PREBIN_26  0x02000000
#define IPC_IMG_PREBIN_27  0x04000000
#define IPC_IMG_PREBIN_28  0x08000000
#define IPC_IMG_PREBIN_29  0x10000000
#define IPC_IMG_PREBIN_30  0x20000000
#define IPC_IMG_PREBIN_31  0x40000000
#define IPC_IMG_PREBIN_32  0x80000000
/** \} */

/*! \name Filter/Aperture info struct
 */
struct filtaper
{
  //! Name of filter/aperture (really only for user convenience)
  char name[IPC_MAX_FILTAPER_NAME_LEN];
  //! Slot where filter/aperture currently installed
  unsigned char slot;
  //! Internal identifier for filter/aperture (used for storage)
  int db_id;
};

/*! \name CCD window mode
 */
struct ipc_ccd_window_mode
{
  /// Width of image in this mode
  unsigned short width_px;
  /// Height of image in this mode
  unsigned short height_px;
  /// X-origin
  unsigned short origin_x;
  /// Y-origin
  unsigned short origin_y;
};

/*! \name Message structures
 *  \{
 */

//! IPC message structure for quit command
struct act_msg_quit
{
  // Automatically close down telescope system
  unsigned char mode_auto;
};

//! IPC message structure for client capabilities/requirements handshake
struct act_msg_cap
{
  //! Client provides. Bitwise OR'd list of services/data client provides.
  unsigned int service_provides;
  //!  Client requires. Bitwise OR'd list of services/data client requires.
  unsigned int service_needs;
  //! Target set role. One of the TARGSET_* flags defined above.
  unsigned short targset_prov;
  //! Data collection with PMT role. One of the DATAPMT_* flags defined above.
  unsigned short datapmt_prov;
  //! Data collection with CCD role. One of the DATACCD_* flags defined above.
  unsigned short dataccd_prov;
  //! Version string of sub-programme.
  char version_str[MAX_VERSION_LEN];
};

//! IPC message structure for client status
struct act_msg_stat
{
  //! Client status.
  char status;
};

//! IPC message structure for X11 socket handshake.
struct act_msg_guisock
{
  //! X11 socket (window ID) number.
  unsigned int gui_socket;
  //! X11 display on which the X11 socket (window) is displayed.
  char display[MAX_XDISPID_LEN];
};

//! IPC message structure for reporting current telescope coordinates
struct act_msg_coord
{
  //! Right ascension.
  struct rastruct ra;
  //! Hour-angle.
  struct hastruct ha;
  //! Declination.
  struct decstruct dec;
  //! Altitude.
  struct altstruct alt;
  //! Azimuth.
  struct azmstruct azm;
  //! Epoch - fractional years since 0 AD
  float epoch;
};

//! IPC message structure for reporting the current time
struct act_msg_time
{
  //! Local Time
  struct timestruct loct;
  //! Universal Time
  struct timestruct unit;
  //! Sidereal Time
  struct timestruct sidt;
  //! Local Date
  struct datestruct locd;
  //! Universal Date
  struct datestruct unid;
  //! Geocentric Julian date in fractional days.
  double gjd;
  //! Heliocentric Julian date in fractional days.
  double hjd;
};

//! IPC message structure for reporting current environmental/situational conditions
struct act_msg_environ
{
  //! Active state. One of the ACTIVE_TIME_* flags defined above.
  unsigned char status_active;
  //! Weather conditions - ==0 if unsafe to observe, >0 otherwise
  unsigned char weath_ok;
  //! Relative humidity in percent.
  unsigned char humidity;
  //! Cloud coverage in percent.
  unsigned char clouds;
  //! Rain warning - TRUE if warning asserted.
  unsigned char rain;
  //! Altitude of Sun in fractional degrees.
  struct altstruct sun_alt;
  //! Right ascension of Moon.
  struct rastruct moon_ra;
  //! Declination of Moon.
  struct decstruct moon_dec;
  //! Wind velocity in fractional km/h.
  float wind_vel;
  //! Wind direction (azimuth)
  struct azmstruct wind_azm;
  //! Size of PSF in arcseconds.
  unsigned short psf_asec;
};

//! IPC message structure for target set capabilities.
struct act_msg_targcap
{
  //! Autoguiding available
  unsigned char autoguide;
  //! Target set stage.
  unsigned short targset_stage;
  //! Western and Eastern hour angle limits
  struct hastruct ha_lim_W, ha_lim_E;
  //! Southern and Northern declination limits
  struct decstruct dec_lim_S, dec_lim_N;
  //! Altitude limit
  struct altstruct alt_lim;
};

//! IPC message structure for target set cycle.
struct act_msg_targset
{
  //! Observation status.
  unsigned char status;
  //! Target set stage.
  unsigned short targset_stage;
  //! Automatic/robotic mode
  unsigned char mode_auto;
  //! Internal target identifier
  int targ_id;
  //! Human-readable name for target
  char targ_name[MAX_TARGID_LEN];
  //! Star (FALSE) or Sky (TRUE)
  char sky;
  //! Right-ascension of target
  struct rastruct targ_ra;
  //! Declination of target
  struct decstruct targ_dec;
  //! Epoch of target in fractional years since 0 AD.
  float targ_epoch;
  //! Adjustment in hour angle from ra
  struct rastruct adj_ra;
  //! Adjustment in declination from dec
  struct decstruct adj_dec;
  //! TRUE if target is centred, 0 (FALSE) otherwise
  unsigned char targ_cent;
  //! Focus position - will be used for auto focus.
  short focus_pos;
  //! Activate autoguiding
  unsigned char autoguide;
};

//! IPC message for PMT photometry capabilities.
struct act_msg_pmtcap
{
  //! PMT identifier
  char pmt_id[IPC_MAX_INSTRID_LEN];
  //! PMT data stage.
  unsigned short datapmt_stage;
  //! Available PMT modes.
  unsigned char pmt_mode;
  //! Minimum and maximum sample period in nanoseconds.
  float min_sample_period_s, max_sample_period_s;
  //! Names of supported filters
  struct filtaper filters[IPC_MAX_NUM_FILTAPERS];
  //! Names of supported apertures
  struct filtaper apertures[IPC_MAX_NUM_FILTAPERS];
};

//! IPC message structure for PMT data collection cycle.
struct act_msg_datapmt
{
  //! Observation status.
  unsigned char status;
  //! PMT data stage.
  unsigned short datapmt_stage;
  //! Automatic/robotic mode
  unsigned char mode_auto;
  //! Internal identifier for user
  int user_id;
  //! Interal target identifier.
  int targ_id;
  //! Human-readable name for target
  char targ_name[MAX_TARGID_LEN];
  //! Star (FALSE) or Sky (TRUE)
  char sky;
  //! Right-ascension of target.
  struct rastruct targ_ra;
  //! Declination of target.
  struct decstruct targ_dec;
  //! Epoch of target coordinates in fractional years since 0 AD.
  float targ_epoch;
  //! Desired PMT mode (must have been defined in pmt_mode in pmtcap message).
  unsigned char pmt_mode;
  //! Sample period in nanoseconds.
  float sample_period_s;
  //! Number of samples to prebin
  unsigned long prebin_num;
  //! Number of repetitions in milli-seconds.
  unsigned long repetitions;
  //! Filter info structure
  struct filtaper filter;
  //! Aperture info structure
  struct filtaper aperture;
};

//! IPC message for CCD photometry capabilities.
struct act_msg_ccdcap
{
  //! Minimum and maximum exposure time supported in milli-seconds.
  unsigned long min_exp_t_msec, max_exp_t_msec;
  //! CCD data collection stage.
  unsigned short dataccd_stage;
  //! CCD identifier
  char ccd_id[IPC_MAX_INSTRID_LEN];
  //! Names of supported filters
  struct filtaper filters[IPC_MAX_NUM_FILTAPERS];
  //! Prebinning modes supported by the CCD - must be a bitwise OR'd list of IPC_IMG_PREBIN_* modes
  unsigned int prebin_x, prebin_y;
  //! Windowing modes supported by the CCD
  struct ipc_ccd_window_mode windows[IPC_CCD_MAX_NUM_WINDOW_MODES];
};

/// TODO: Include supported frame transfer modes in CCD caps

//! IPC message structure for CCD data collection cycle.
struct act_msg_dataccd
{
  //! Observation status.
  unsigned char status;
  //! CCD data collection stage.
  unsigned short dataccd_stage;
  //! Automatic/robotic mode
  unsigned char mode_auto;
  //! Internal identifier for user
  int user_id;
  //! Internal target identifier.
  int targ_id;
  //! Human-readable name for target
  char targ_name[MAX_TARGID_LEN];
  //! Right-ascension of target.
  struct rastruct targ_ra;
  //! Declination of target.
  struct decstruct targ_dec;
  //! Epoch of target coordinates in fractional years since 0 AD.
  float targ_epoch;
  //! Integration time in milli-seconds.
  double exp_t_s;
  //! Number of repetitions in milli-seconds.
  unsigned long repetitions;
  //! Filter info structure
  struct filtaper filter;
  //! The desired frame transfer mode (must have been defined in frame_transfer of ccdcap message)
  unsigned char frame_transfer;
  //! The desired prebinning mode (must be one of the bits specified in ccdcap)
  unsigned int prebin_x, prebin_y;
  //! Desired CCD window mode number (element in windows array supplied in ccdcap message)
  unsigned char window_mode_num;
};

/** \} */

//! Union of all message structures
union act_msg_data
{
  struct act_msg_quit msg_quit;
  struct act_msg_cap msg_cap;
  struct act_msg_stat msg_stat;
  struct act_msg_guisock msg_guisock;
  struct act_msg_coord msg_coord;
  struct act_msg_time msg_time;
  struct act_msg_environ msg_environ;
  struct act_msg_targcap msg_targcap;
  struct act_msg_targset msg_targset;
  struct act_msg_pmtcap msg_pmtcap;
  struct act_msg_datapmt msg_datapmt;
  struct act_msg_ccdcap msg_ccdcap;
  struct act_msg_dataccd msg_dataccd;
};

/*! \name Message Type enum
 * \brief Enumerations for IPC message structure types.
 *
 * Messages passed between programmes are passed as C "tagged" unions. The mtype flag of the act_msg
 * structure has one of the following values, which indicates which field of the act_msg_data union
 * is in use.
 */
/*! \{ */
enum
{
  MT_QUIT = 1,       /**< User quit - sent globally so all clients close down and quit.*/
  MT_CAP,            /**< Client capabilities request/response.*/
  MT_STAT,           /**< Current programme status.*/
  MT_GUISOCK,        /**< X11 socket handshake.*/
  MT_COORD,          /**< Telescope coordinates.*/
  MT_TIME,           /**< Current time.*/
  MT_ENVIRON,        /**< Environment/situational parameters.*/
  MT_TARG_CAP,       /**< Capabilities for setting target (software telescope limits etc.) */
  MT_TARG_SET,       /**< Target acquisition cycle message.*/
  MT_PMT_CAP,        /**< Photometric capabilities with PMT */
  MT_DATA_PMT,       /**< Data collection with PMT cycle message.*/
  MT_CCD_CAP,        /**< Photometric capabilities with CCD */
  MT_DATA_CCD,       /**< Data collection with CCD cycle message.*/
  MT_INVAL           /**< Trunc - any message type greater than or equal to this number is invalid.*/
};
/*! \} */


//! IPC message tagged union
struct act_msg
{
  //! Message type.
  char mtype;
  //! Message content
  union act_msg_data content;
};

#endif
