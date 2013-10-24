#ifndef PMT_DEFS_H
#define PMT_DEFS_H

/// Maximum length for PMT identifier string
#define MAX_PMT_ID_LEN    10

/** \brief PMT Modes
 * \{ 
 */
/// Integration mode
#define PMT_MODE_INTEG    0x01
/// Time-tag mode
#define PMT_MODE_TTAG     0x02
/** \} */

/** \brief PMT Error bits
 * \{
 */
/// Zero Counts
#define PMT_ERR_ZERO       0x01
/// High counts, potential/imminent PMT damage
#define PMT_ERR_WARN       0x02
/// Countrate too high - emergency closure is triggered
#define PMT_ERR_OVERILLUM  0x04
/// Counter overflow - used only during probe stage. Counter overflow may occurr with specified PMT sample rate.
#define PMT_ERR_OVERFLOW   0x08
/// Time synchronised
#define PMT_ERR_TIME_SYNC  0x10
/// Mask to select PMT critical error bits
#define PMT_CRIT_ERR_MASK  (PMT_ERR_ZERO | PMT_ERR_OVERILLUM | PMT_ERR_OVERFLOW)
/// Alert conditions that are generated externally
#define PMT_EXT_ERR_MASK   PMT_ERR_TIME_SYNC
/** \} */

/** \brief PMT Driver status bits
 * \{
 */
/// PMT is being probed for overillumination
#define PMT_STAT_PROBE      0x01
/// Busy collecting data
#define PMT_STAT_BUSY       0x02
/// Data ready to be read
#define PMT_STAT_DATA_READY 0x04
/// An error has been raised
#define PMT_STAT_ERR        0x08
/// Flag activated when a new status available
#define PMT_STAT_UPDATE     0x10
/** \} */

/** \brief PMT information structure.
 * Contains all necessary information required to effectively use the PMT.
 * Structure fields are populated as appropriate by driver.
 * \{
 */
struct pmt_information
{
  /// PMT identifier string.
  char pmt_id[MAX_PMT_ID_LEN];
  /// Modes supported by PMT/driver - bitwise OR'd list of the PMT Modes listed above.
  unsigned char modes;
  /// Minimum time resolution for time-tagging photons in nanoseconds - used only if time-tag mode supported.
  unsigned long timetag_time_res_ns;
  /// Maximum and minimum sampling periods for PMT integrations in nanoseconds - used only if integration mode supported.
  unsigned long max_sample_period_ns, min_sample_period_ns;
  /// PMT counter dead time in nanoseconds - used only if integration mode supported.
  unsigned long dead_time_ns;
};
/** \} */

/** \brief PMT command structure
 * User-space programme populates this structure as appropriate and sends it to driver.
 * \{
 */
struct pmt_command
{
  /// PMT Mode - must be one of the supported PMT Modes or 0 to cancel data collection.
  char mode;
  /// Sampling period in nanoseconds - must be within limits given in pmt_information structure.
  unsigned long sample_length;
  /// Prebinning - driver will bin this many samples.
  unsigned long prebin_num;
  /// Repetitions - only used with integration mode.
  unsigned long repetitions;
};
/** \} */

/** \brief PMT Integration data structure
 * \{
 */
struct pmt_integ_data
{
  /// Universal time at integration start - second and nanosecond components.
  unsigned long start_time_s, start_time_ns;
  /// Used sample period.
  unsigned long sample_period_ns;
  /// Number of samples per bin
  unsigned long prebin_num;
  /// Number of repetitions completed
  unsigned long repetitions;
  /// PMT counts
  unsigned long counts;
  /// Status of PMT/driver during integration
  unsigned char error;
};
/** \} */

/** \brief PMT time tag data structure
 * \{
 */
struct pmt_ttag_data
{
  /// Universal time at photon arrival - second and nanosecond components.
  unsigned long time_s, time_ns;
};
/** \} */

#endif