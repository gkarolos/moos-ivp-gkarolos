/************************************************************/
/*    NAME: Karolos_Geroulanos                                              */
/*    ORGN: MIT, Cambridge MA                               */
/*    FILE: Odometry.cpp                                        */
/*    DATE: December 29th, 1963                             */
/************************************************************/

#include <iterator>
#include <cmath>
#include "MBUtils.h"
#include "ACTable.h"
#include "Odometry.h"

using namespace std;

//---------------------------------------------------------
// Constructor()

Odometry::Odometry()
{
  m_first_reading  = true;
  m_current_x      = 0;
  m_current_y      = 0;
  m_previous_x     = 0;
  m_previous_y     = 0;
  m_total_distance = 0;

  m_got_x = false;
  m_got_y = false;
}

//---------------------------------------------------------
// Destructor

Odometry::~Odometry()
{
}

//---------------------------------------------------------
// Procedure: OnNewMail()

bool Odometry::OnNewMail(MOOSMSG_LIST &NewMail)
{
  AppCastingMOOSApp::OnNewMail(NewMail);

  MOOSMSG_LIST::iterator p;
  for(p=NewMail.begin(); p!=NewMail.end(); p++) {
    CMOOSMsg &msg = *p;
    string key    = msg.GetKey();

#if 0 // Keep these around just for template
    string comm  = msg.GetCommunity();
    double dval  = msg.GetDouble();
    string sval  = msg.GetString(); 
    string msrc  = msg.GetSource();
    double mtime = msg.GetTime();
    bool   mdbl  = msg.IsDouble();
    bool   mstr  = msg.IsString();
#endif

    if(key == "NAV_X")      { m_current_x = msg.GetDouble(); m_got_x = true; }
    else if(key == "NAV_Y") { m_current_y = msg.GetDouble(); m_got_y = true; }

    else if(key != "APPCAST_REQ") // handled by AppCastingMOOSApp
      reportRunWarning("Unhandled Mail: " + key);

    // Got a complete, fresh (x,y) pair? Add this leg to the running total.
    // Done here in OnNewMail (not Iterate) so distance is correct even though
    // NAV posts at 10 Hz while Iterate runs at 4 Hz.
    if(m_got_x && m_got_y) {
      if(!m_first_reading)
        m_total_distance += hypot(m_current_x - m_previous_x,
                                  m_current_y - m_previous_y);
      m_first_reading = false;
      m_previous_x = m_current_x;
      m_previous_y = m_current_y;
      m_got_x = false;
      m_got_y = false;
    }
  }
	
   return(true);
}

//---------------------------------------------------------
// Procedure: OnConnectToServer()

bool Odometry::OnConnectToServer()
{
   registerVariables();
   return(true);
}

//---------------------------------------------------------
// Procedure: Iterate()
//            happens AppTick times per second

bool Odometry::Iterate()
{
  AppCastingMOOSApp::Iterate();

  Notify("ODOMETRY_DIST", m_total_distance);

  AppCastingMOOSApp::PostReport();
  return(true);
}

//---------------------------------------------------------
// Procedure: OnStartUp()
//            happens before connection is open

bool Odometry::OnStartUp()
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
    if(param == "foo") {
      handled = true;
    }
    else if(param == "bar") {
      handled = true;
    }

    if(!handled)
      reportUnhandledConfigWarning(orig);

  }
  
  registerVariables();	
  return(true);
}

//---------------------------------------------------------
// Procedure: registerVariables()

void Odometry::registerVariables()
{
  AppCastingMOOSApp::RegisterVariables();
  Register("NAV_X", 0);
  Register("NAV_Y", 0);
}


//------------------------------------------------------------
// Procedure: buildReport()

bool Odometry::buildReport() 
{
  m_msgs << "Total distance traveled: " << doubleToString(m_total_distance, 2) << " m" << endl;
  m_msgs << "Current position (x, y): (" << doubleToString(m_current_x, 2)
         << ", " << doubleToString(m_current_y, 2) << ")" << endl;
  m_msgs << "First reading received:  " << (m_first_reading ? "no (waiting)" : "yes") << endl;

  return(true);
}




