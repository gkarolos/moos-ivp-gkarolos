/************************************************************/
/*    NAME: Karolos Geroulanos                              */
/*    ORGN: MIT, Cambridge MA                               */
/*    FILE: PointAssign.cpp                                 */
/*    DATE: 2026                                            */
/************************************************************/

#include <iterator>
#include "MBUtils.h"
#include "ACTable.h"
#include "PointAssign.h"
#include "XYPoint.h"

using namespace std;

//---------------------------------------------------------
// Constructor()

PointAssign::PointAssign()
{
  // Configuration defaults
  m_assign_by_region = false;       // default mode = alternating round-robin
  m_region_split_x   = 87.5;        // midpoint of region x in [-25, 200]

  // State
  m_points_total   = 0;
  m_assign_index   = 0;
  m_got_firstpoint = false;
  m_got_lastpoint  = false;
}

//---------------------------------------------------------
// Destructor

PointAssign::~PointAssign()
{
}

//---------------------------------------------------------
// Procedure: OnNewMail()

bool PointAssign::OnNewMail(MOOSMSG_LIST &NewMail)
{
  AppCastingMOOSApp::OnNewMail(NewMail);

  MOOSMSG_LIST::iterator p;
  for(p=NewMail.begin(); p!=NewMail.end(); p++) {
    CMOOSMsg &msg = *p;
    string key    = msg.GetKey();
    string sval   = msg.GetString();

    if(key == "VISIT_POINT")
      handleMailVisitPoint(sval);

    else if(key != "APPCAST_REQ") // handled by AppCastingMOOSApp
      reportRunWarning("Unhandled Mail: " + key);
   }

   return(true);
}

//---------------------------------------------------------
// Procedure: handleMailVisitPoint()
//   Incoming VISIT_POINT is one of:
//     "firstpoint"            (completeness cue: broadcast to all vehicles)
//     "lastpoint"             (completeness cue: broadcast to all vehicles)
//     "x=..,y=..,id=.."       (a real point to assign to one vehicle)

void PointAssign::handleMailVisitPoint(const string& sval)
{
  // -- Completeness cues: relay to EVERY vehicle's VISIT_POINT_<VNAME> --
  if(sval == "firstpoint") {
    m_got_firstpoint = true;
    for(unsigned int i=0; i<m_vnames.size(); i++)
      Notify("VISIT_POINT_" + toupper(m_vnames[i]), "firstpoint");
    return;
  }
  if(sval == "lastpoint") {
    m_got_lastpoint = true;
    for(unsigned int i=0; i<m_vnames.size(); i++)
      Notify("VISIT_POINT_" + toupper(m_vnames[i]), "lastpoint");
    return;
  }

  // -- A real point: parse x, y (id stays inside the forwarded spec) --
  string xstr, ystr;
  bool ok_x = tokParse(sval, "x", ',', '=', xstr);
  bool ok_y = tokParse(sval, "y", ',', '=', ystr);
  if(!ok_x || !ok_y) {
    reportRunWarning("Malformed VISIT_POINT: " + sval);
    return;
  }
  double x = atof(xstr.c_str());
  double y = atof(ystr.c_str());

  m_points_total++;
  m_last_point = sval;
  assignPoint(sval, x, y);
}

//---------------------------------------------------------
// Procedure: assignPoint()
//   Pick a vehicle for this point, forward the ORIGINAL spec to its
//   VISIT_POINT_<VNAME> (exact format preserved), and post a colored
//   VIEW_POINT so the assignment is visible in pMarineViewer.

void PointAssign::assignPoint(const string& spec, double x, double y)
{
  if(m_vnames.empty()) {
    reportRunWarning("No vname configured: cannot assign points");
    return;
  }

  unsigned int idx = 0;   // index into m_vnames of the chosen vehicle

  if(m_assign_by_region && (m_vnames.size() >= 2)) {
    // West half -> first vehicle, East half -> second vehicle
    idx = (x < m_region_split_x) ? 0 : 1;
  }
  else {
    // Alternating round-robin across all configured vehicles
    idx = m_assign_index % m_vnames.size();
  }
  m_assign_index++;

  string vname = m_vnames[idx];
  Notify("VISIT_POINT_" + toupper(vname), spec);
  m_vname_count[vname]++;

  // Visual: unique label per point (use the id field so labels never collide)
  string idstr;
  string label = vname;
  if(tokParse(spec, "id", ',', '=', idstr))
    label = vname + "_" + idstr;
  postViewPoint(x, y, label, colorForIndex(idx));
}

//---------------------------------------------------------
// Procedure: postViewPoint()

void PointAssign::postViewPoint(double x, double y, const string& label,
                                const string& color)
{
  XYPoint pt(x, y);
  pt.set_label(label);             // must be unique or the viewer overwrites
  pt.set_color("vertex", color);   // one color per vehicle
  pt.set_param("vertex_size", "4");
  Notify("VIEW_POINT", pt.get_spec());
}

//---------------------------------------------------------
// Procedure: colorForIndex()
//   Stable palette so each vehicle keeps its own color.

string PointAssign::colorForIndex(unsigned int index) const
{
  const char* palette[] = {"yellow", "dodger_blue", "green",
                           "red", "orange", "magenta"};
  unsigned int n = sizeof(palette) / sizeof(palette[0]);
  return(palette[index % n]);
}

//---------------------------------------------------------
// Procedure: OnConnectToServer()

bool PointAssign::OnConnectToServer()
{
   registerVariables();
   return(true);
}

//---------------------------------------------------------
// Procedure: Iterate()
//            happens AppTick times per second

bool PointAssign::Iterate()
{
  AppCastingMOOSApp::Iterate();
  // All work is event-driven in OnNewMail(); nothing to do per-tick.
  AppCastingMOOSApp::PostReport();
  return(true);
}

//---------------------------------------------------------
// Procedure: OnStartUp()
//            happens before connection is open

bool PointAssign::OnStartUp()
{
  AppCastingMOOSApp::OnStartUp();

  STRING_LIST sParams;
  m_MissionReader.EnableVerbatimQuoting(false);
  if(!m_MissionReader.GetConfiguration(GetAppName(), sParams))
    reportConfigWarning("No config block found for " + GetAppName());

  STRING_LIST::iterator p;
  for(p=sParams.begin(); p!=sParams.end(); p++) {
    string orig  = *p;
    string line  = *p;
    string param = tolower(biteStringX(line, '='));
    string value = line;

    bool handled = false;
    if(param == "vname") {
      // One vname= line per vehicle; builds the distribute list.
      if(value != "") {
        m_vnames.push_back(value);
        handled = true;
      }
    }
    else if(param == "assign_by_region") {
      handled = setBooleanOnString(m_assign_by_region, value);
    }
    else if(param == "region_split_x") {
      handled = setDoubleOnString(m_region_split_x, value);
    }

    if(!handled)
      reportUnhandledConfigWarning(orig);
  }

  registerVariables();
  return(true);
}

//---------------------------------------------------------
// Procedure: registerVariables()

void PointAssign::registerVariables()
{
  AppCastingMOOSApp::RegisterVariables();
  Register("VISIT_POINT", 0);
}


//------------------------------------------------------------
// Procedure: buildReport()

bool PointAssign::buildReport()
{
  string mode = m_assign_by_region ? "by-region (west/east)" : "alternating";

  m_msgs << "Config:" << endl;
  m_msgs << "  Mode:           " << mode << endl;
  if(m_assign_by_region)
    m_msgs << "  Region split x: " << doubleToStringX(m_region_split_x,1) << endl;
  m_msgs << "  Vehicles:       " << uintToString(m_vnames.size()) << endl;
  m_msgs << "============================================" << endl;
  m_msgs << "Points received:  " << uintToString(m_points_total) << endl;
  m_msgs << "  firstpoint cue: " << (m_got_firstpoint ? "yes" : "no") << endl;
  m_msgs << "  lastpoint cue:  " << (m_got_lastpoint  ? "yes" : "no") << endl;
  m_msgs << "  last point:     " << m_last_point << endl;
  m_msgs << "============================================" << endl;

  ACTable actab(3);
  actab << "Vehicle | Color | Points";
  actab.addHeaderLines();
  for(unsigned int i=0; i<m_vnames.size(); i++) {
    string vname = m_vnames[i];
    actab << vname << colorForIndex(i) << uintToString(m_vname_count[vname]);
  }
  m_msgs << actab.getFormattedString();

  return(true);
}
