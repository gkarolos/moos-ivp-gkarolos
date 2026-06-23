/************************************************************/
/*    NAME: Karolos Geroulanos                                              */
/*    ORGN: MIT                                             */
/*    FILE: BHV_Pulse.h                                      */
/*    DATE:                                                 */
/************************************************************/

#ifndef Pulse_HEADER
#define Pulse_HEADER

#include <string>
#include "IvPBehavior.h"

class BHV_Pulse : public IvPBehavior {
public:
  BHV_Pulse(IvPDomain);
  ~BHV_Pulse() {};
  
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
  double m_pulse_range;      // radius the pulse expands to (meters)
  double m_pulse_duration;   // how long the pulse animates (seconds)

protected: // State variables
  double m_osx;              // ownship x, from NAV_X
  double m_osy;              // ownship y, from NAV_Y
  double m_prev_index;       // last WPT_INDEX value we saw
  bool   m_index_set;        // have we seen a WPT_INDEX yet?
  bool   m_pulse_pending;    // a waypoint was hit; pulse not yet fired
  double m_wpt_hit_utc;      // time the waypoint was hit (for the 5s delay)
};

#define IVP_EXPORT_FUNCTION

extern "C" {
  IVP_EXPORT_FUNCTION IvPBehavior * createBehavior(std::string name, IvPDomain domain) 
  {return new BHV_Pulse(domain);}
}
#endif
