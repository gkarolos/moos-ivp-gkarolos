/************************************************************/
/*    NAME: Karolos Geroulanos                              */
/*    ORGN: MIT                                             */
/*    FILE: BHV_ZigLeg.h                                    */
/*    DATE: 2026-06-23                                      */
/************************************************************/

#ifndef ZigLeg_HEADER
#define ZigLeg_HEADER

#include <string>
#include "IvPBehavior.h"

class BHV_ZigLeg : public IvPBehavior {
public:
  BHV_ZigLeg(IvPDomain);
  ~BHV_ZigLeg() {};

  bool         setParam(std::string, std::string);
  void         onSetParamComplete();
  void         onCompleteState();
  void         onIdleState();
  void         onHelmStart();
  void         postConfigStatus();
  void         onRunToIdleState();
  void         onIdleToRunState();
  IvPFunction* onRunState();

protected: // Local Utility functions

protected: // Configuration parameters
  double m_zig_angle;        // how far off heading to zig (degrees)
  double m_zig_duration;     // how long to hold the zig (seconds)

protected: // State variables
  double m_osx;              // ownship x, from NAV_X  (for the marker pulse)
  double m_osy;              // ownship y, from NAV_Y  (for the marker pulse)
  double m_osh;              // ownship heading, from NAV_HEADING
  double m_prev_index;       // last WPT_INDEX seen; starts at 0 so the FIRST
                             // waypoint capture also counts as a change
  bool   m_zig_pending;      // a waypoint was hit; zig not yet started
  double m_wpt_hit_utc;      // time the waypoint was hit (for the delay)
  bool   m_zig_active;       // currently producing the zig objective function
  double m_zig_start_utc;    // time the zig started (for the duration)
  double m_zig_heading;      // fixed target heading held during the zig (deg)
};

#define IVP_EXPORT_FUNCTION

extern "C" {
  IVP_EXPORT_FUNCTION IvPBehavior * createBehavior(std::string name, IvPDomain domain)
  {return new BHV_ZigLeg(domain);}
}
#endif
