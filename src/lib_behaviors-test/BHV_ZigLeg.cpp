/************************************************************/
/*    NAME: Karolos Geroulanos                              */
/*    ORGN: MIT                                             */
/*    FILE: BHV_ZigLeg.cpp                                  */
/*    DATE: 2026-06-23                                      */
/************************************************************/

#include <iterator>
#include <cstdlib>
#include "MBUtils.h"
#include "BuildUtils.h"
#include "AngleUtils.h"
#include "ZAIC_PEAK.h"
#include "BHV_ZigLeg.h"

using namespace std;

// Seconds to wait after a waypoint is hit before starting the zig.
static const double ZIG_DELAY = 5.0;

//---------------------------------------------------------------
// Constructor

BHV_ZigLeg::BHV_ZigLeg(IvPDomain domain) :
  IvPBehavior(domain)
{
  // Provide a default behavior name
  IvPBehavior::setParam("name", "defaultname");

  // This behavior STEERS the vehicle, so it produces an objective function
  // over the "course" (heading) decision variable. (BHV_Pulse, which only
  // posts a visual, dropped the domain; ZigLeg needs it back.)
  m_domain = subDomain(m_domain, "course");

  // Default values for configuration parameters
  m_zig_angle    = 45;
  m_zig_duration = 10;

  // Default values for behavior state variables
  m_osx           = 0;
  m_osy           = 0;
  m_osh           = 0;
  m_prev_index    = 0;        // boat starts heading to waypoint 0
  m_zig_pending   = false;
  m_wpt_hit_utc   = 0;
  m_zig_active    = false;
  m_zig_start_utc = 0;
  m_zig_heading   = 0;

  // Subscribe for ownship position + heading and the sister waypoint
  // behavior's current waypoint index.
  addInfoVars("NAV_X, NAV_Y, NAV_HEADING, WPT_INDEX");
}

//---------------------------------------------------------------
// Procedure: setParam()

bool BHV_ZigLeg::setParam(string param, string val)
{
  // Let the base class handle standard params (name, pwt, condition, ...)
  if(IvPBehavior::setParam(param, val))
    return(true);

  param = tolower(param);
  double double_val = atof(val.c_str());

  // zig_angle may be negative (zig to the other side), so no >0 check.
  if((param == "zig_angle") && isNumber(val)) {
    m_zig_angle = double_val;
    return(true);
  }
  else if((param == "zig_duration") && isNumber(val) && (double_val > 0)) {
    m_zig_duration = double_val;
    return(true);
  }

  // If not handled above, then just return false;
  return(false);
}

//---------------------------------------------------------------
// Procedure: onSetParamComplete()
//   Purpose: Invoked once after all parameters have been handled.

void BHV_ZigLeg::onSetParamComplete()
{
  if(m_zig_duration <= 0)
    postEMessage("zig_duration must be greater than zero");
}

//---------------------------------------------------------------
// Procedure: onHelmStart()

void BHV_ZigLeg::onHelmStart()
{
}

//---------------------------------------------------------------
// Procedure: onIdleState()

void BHV_ZigLeg::onIdleState()
{
}

//---------------------------------------------------------------
// Procedure: onCompleteState()

void BHV_ZigLeg::onCompleteState()
{
}

//---------------------------------------------------------------
// Procedure: postConfigStatus()

void BHV_ZigLeg::postConfigStatus()
{
}

//---------------------------------------------------------------
// Procedure: onIdleToRunState()

void BHV_ZigLeg::onIdleToRunState()
{
}

//---------------------------------------------------------------
// Procedure: onRunToIdleState()
//   Cancel any in-progress / pending zig so it doesn't resume stale.

void BHV_ZigLeg::onRunToIdleState()
{
  m_zig_pending = false;
  m_zig_active  = false;
}

//---------------------------------------------------------------
// Procedure: onRunState()
//   Purpose: Watch the waypoint index. ZIG_DELAY seconds after a waypoint is
//            reached, start a "zig": capture (current heading + zig_angle) as a
//            FIXED target and hold it for zig_duration seconds via a course
//            ZAIC_PEAK objective function, nudging the boat off its track.

IvPFunction* BHV_ZigLeg::onRunState()
{
  // Part 1: Read ownship heading (required) plus position (for the marker
  // pulse) and the current buffer time.
  bool ok_h, ok_x, ok_y;
  m_osh = getBufferDoubleVal("NAV_HEADING", ok_h);
  m_osx = getBufferDoubleVal("NAV_X", ok_x);
  m_osy = getBufferDoubleVal("NAV_Y", ok_y);
  if(!ok_h) {
    postWMessage("No ownship NAV_HEADING in info buffer.");
    return(0);
  }
  double curr_time = getBufferCurrTime();

  // Part 2: Watch the waypoint index posted by the sister waypoint behavior.
  // m_prev_index starts at 0 (the waypoint the boat first heads to), so when
  // the boat captures it and the index leaves 0, that FIRST capture also counts
  // as a change and arms a zig -- the same as every later waypoint.
  bool ok_i;
  double curr_index = getBufferDoubleVal("WPT_INDEX", ok_i);
  if(ok_i && (curr_index != m_prev_index)) {
    // The index changed -> a waypoint was just reached. Arm a zig; it will
    // start ZIG_DELAY seconds from now.
    m_prev_index  = curr_index;
    m_wpt_hit_utc = curr_time;
    m_zig_pending = true;
  }

  // Part 3: When the delay elapses, start the zig and capture a FIXED target
  // heading (current heading + zig_angle). Holding a fixed target keeps the
  // boat from chasing an ever-moving summit and turning forever.
  if(m_zig_pending && ((curr_time - m_wpt_hit_utc) >= ZIG_DELAY)) {
    m_zig_heading   = angle360(m_osh + m_zig_angle);
    m_zig_active    = true;
    m_zig_start_utc = curr_time;
    m_zig_pending   = false;

    // Mark the moment the zig starts with a visible range pulse (same hand-built
    // spec as BHV_Pulse -- no ColorPack, so the helm won't crash). This makes
    // EVERY trigger visible, including the first waypoint where the heading zig
    // can otherwise blend into the boat's natural turn onto the next leg.
    string spec;
    spec  = "x="         + doubleToStringX(m_osx, 2);
    spec += ",y="        + doubleToStringX(m_osy, 2);
    spec += ",radius=20,duration=4";
    spec += ",fill=0.25,label=zigleg_pulse";
    spec += ",edge_color=green,fill_color=green,edge_size=1";
    spec += ",time="     + doubleToStringX(curr_time, 2);
    postMessage("VIEW_RANGE_PULSE", spec);

    // Debug breadcrumb so each fire is unmistakable in the alog.
    postMessage("ZIGLEG_FIRE", "index=" + doubleToStringX(m_prev_index,0) +
                ",heading=" + doubleToStringX(m_zig_heading,1));
  }

  // Part 4: While the zig is active, return a course objective function peaked
  // at the target heading. After zig_duration, stop and let the waypoint
  // behavior steer again.
  if(m_zig_active) {
    if((curr_time - m_zig_start_utc) >= m_zig_duration) {
      m_zig_active = false;
      return(0);
    }

    ZAIC_PEAK zaic(m_domain, "course");
    zaic.setSummit(m_zig_heading);
    zaic.setPeakWidth(0);
    zaic.setBaseWidth(180);
    zaic.setValueWrap(true);     // course is circular: 0 and 360 are the same

    IvPFunction* ipf = zaic.extractIvPFunction();
    if(ipf)
      ipf->setPWT(m_priority_wt);
    return(ipf);
  }

  // Not zigging right now: contribute nothing to the helm decision.
  return(0);
}
