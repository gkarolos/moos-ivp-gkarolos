/************************************************************/
/*    NAME: Karolos Geroulanos                              */
/*    ORGN: MIT, Cambridge MA                               */
/*    FILE: GenPath.cpp                                     */
/*    DATE: 2026                                            */
/************************************************************/

#include <iterator>
#include <cmath>
#include "MBUtils.h"
#include "ACTable.h"
#include "GenPath.h"
#include "XYSegList.h"

using namespace std;

//---------------------------------------------------------
// Constructor()

GenPath::GenPath()
{
  // Configuration defaults
  m_update_var   = "TRANSIT_UPDATE";   // Waypoint behavior's updates= variable
  m_visit_radius = 3.0;                // lab: a visit counts within 3 m

  // State
  m_got_firstpoint = false;
  m_got_lastpoint  = false;
  m_tour_posted    = false;

  m_nav_received = false;
  m_nav_x = 0;
  m_nav_y = 0;
}

//---------------------------------------------------------
// Destructor

GenPath::~GenPath()
{
}

//---------------------------------------------------------
// Procedure: OnNewMail()

bool GenPath::OnNewMail(MOOSMSG_LIST &NewMail)
{
  AppCastingMOOSApp::OnNewMail(NewMail);

  MOOSMSG_LIST::iterator p;
  for(p=NewMail.begin(); p!=NewMail.end(); p++) {
    CMOOSMsg &msg = *p;
    string key    = msg.GetKey();

    if(key == "VISIT_POINT")
      handleMailVisitPoint(msg.GetString());

    else if(key == "NAV_X") {
      m_nav_x = msg.GetDouble();
      m_nav_received = true;
    }
    else if(key == "NAV_Y") {
      m_nav_y = msg.GetDouble();
      m_nav_received = true;
    }

    else if(key != "APPCAST_REQ") // handled by AppCastingMOOSApp
      reportRunWarning("Unhandled Mail: " + key);
   }

   // Keep the visited tally current as the vehicle moves.
   updateVisited();

   return(true);
}

//---------------------------------------------------------
// Procedure: handleMailVisitPoint()
//   VISIT_POINT is one of: "firstpoint", "lastpoint", or "x=..,y=..,id=.."

void GenPath::handleMailVisitPoint(const string& sval)
{
  if(sval == "firstpoint") {
    // Start of a fresh set: reset collection state.
    m_got_firstpoint = true;
    m_got_lastpoint  = false;
    m_tour_posted    = false;
    m_points.clear();
    m_visited.clear();
    return;
  }

  if(sval == "lastpoint") {
    m_got_lastpoint = true;
    // The whole set is in hand; build the tour as soon as we also have a
    // NAV fix (handled in Iterate if NAV has not arrived yet).
    return;
  }

  // A real point. Parse x/y; keep the id only for the visible label.
  string xstr, ystr, idstr;
  bool ok_x = tokParse(sval, "x", ',', '=', xstr);
  bool ok_y = tokParse(sval, "y", ',', '=', ystr);
  if(!ok_x || !ok_y) {
    reportRunWarning("Malformed VISIT_POINT: " + sval);
    return;
  }

  XYPoint pt(atof(xstr.c_str()), atof(ystr.c_str()));
  if(tokParse(sval, "id", ',', '=', idstr))
    pt.set_label(idstr);
  m_points.push_back(pt);
}

//---------------------------------------------------------
// Procedure: buildAndPostTour()
//   Greedy nearest-neighbour tour: start at the current NAV position, then
//   repeatedly hop to the closest not-yet-chosen point. Post the ordered
//   list to the Waypoint behavior's update variable.

void GenPath::buildAndPostTour()
{
  unsigned int n = m_points.size();
  if(n == 0) {
    reportRunWarning("lastpoint received but no points to tour");
    m_tour_posted = true;   // nothing to do; don't keep retrying
    return;
  }

  vector<bool> chosen(n, false);
  double cx = m_nav_x;            // virtual start = current vehicle position
  double cy = m_nav_y;

  XYSegList seglist;
  for(unsigned int step=0; step<n; step++) {
    int    best = -1;
    double best_dist = 0;
    for(unsigned int i=0; i<n; i++) {
      if(chosen[i])
        continue;
      double dx = m_points[i].get_vx() - cx;
      double dy = m_points[i].get_vy() - cy;
      double d  = hypot(dx, dy);
      if((best < 0) || (d < best_dist)) {
        best = (int)i;
        best_dist = d;
      }
    }
    chosen[best] = true;
    cx = m_points[best].get_vx();
    cy = m_points[best].get_vy();
    seglist.add_vertex(cx, cy);
  }

  // Wire format: "points=pts={x=..,y=..:...}" into the behavior's updates var.
  Notify(m_update_var, "points=" + seglist.get_spec_pts());

  // Reset the visited tracker to match the (now ordered) point set.
  m_visited.assign(n, false);
  m_tour_posted = true;
}

//---------------------------------------------------------
// Procedure: updateVisited()
//   Mark any point the vehicle has come within m_visit_radius of as visited.

void GenPath::updateVisited()
{
  if(!m_tour_posted || !m_nav_received)
    return;
  if(m_visited.size() != m_points.size())
    return;

  for(unsigned int i=0; i<m_points.size(); i++) {
    if(m_visited[i])
      continue;
    double dx = m_points[i].get_vx() - m_nav_x;
    double dy = m_points[i].get_vy() - m_nav_y;
    if(hypot(dx, dy) <= m_visit_radius)
      m_visited[i] = true;
  }
}

//---------------------------------------------------------
// Procedure: OnConnectToServer()

bool GenPath::OnConnectToServer()
{
   registerVariables();
   return(true);
}

//---------------------------------------------------------
// Procedure: Iterate()
//            happens AppTick times per second

bool GenPath::Iterate()
{
  AppCastingMOOSApp::Iterate();

  // Build the tour once we have the full set plus a NAV fix. Done here (not in
  // OnNewMail) so a late-arriving NAV still triggers it.
  if(m_got_lastpoint && !m_tour_posted && m_nav_received)
    buildAndPostTour();

  AppCastingMOOSApp::PostReport();
  return(true);
}

//---------------------------------------------------------
// Procedure: OnStartUp()
//            happens before connection is open

bool GenPath::OnStartUp()
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
    if(param == "update_var") {
      if(value != "") {
        m_update_var = value;
        handled = true;
      }
    }
    else if(param == "visit_radius") {
      handled = setDoubleOnString(m_visit_radius, value);
    }

    if(!handled)
      reportUnhandledConfigWarning(orig);
  }

  registerVariables();
  return(true);
}

//---------------------------------------------------------
// Procedure: registerVariables()

void GenPath::registerVariables()
{
  AppCastingMOOSApp::RegisterVariables();
  Register("VISIT_POINT", 0);
  Register("NAV_X", 0);
  Register("NAV_Y", 0);
}


//------------------------------------------------------------
// Procedure: buildReport()

bool GenPath::buildReport()
{
  unsigned int visited = 0;
  for(unsigned int i=0; i<m_visited.size(); i++)
    if(m_visited[i]) visited++;

  unsigned int total = m_points.size();
  unsigned int unvisited = (total > visited) ? (total - visited) : 0;

  m_msgs << "Update var:       " << m_update_var << endl;
  m_msgs << "Visit radius:     " << doubleToStringX(m_visit_radius,1) << " m" << endl;
  m_msgs << "NAV received:     " << (m_nav_received ? "yes" : "no");
  if(m_nav_received)
    m_msgs << "  (" << doubleToStringX(m_nav_x,1) << ", "
           << doubleToStringX(m_nav_y,1) << ")";
  m_msgs << endl;
  m_msgs << "============================================" << endl;
  m_msgs << "firstpoint:       " << (m_got_firstpoint ? "yes" : "no") << endl;
  m_msgs << "lastpoint:        " << (m_got_lastpoint  ? "yes" : "no") << endl;
  m_msgs << "Tour posted:      " << (m_tour_posted    ? "yes" : "no") << endl;
  m_msgs << "============================================" << endl;
  m_msgs << "Points received:  " << uintToString(total)     << endl;
  m_msgs << "Points visited:   " << uintToString(visited)   << endl;
  m_msgs << "Points unvisited: " << uintToString(unvisited) << endl;

  return(true);
}
