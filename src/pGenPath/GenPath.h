/************************************************************/
/*    NAME: Karolos Geroulanos                              */
/*    ORGN: MIT, Cambridge MA                               */
/*    FILE: GenPath.h                                       */
/*    DATE: 2026                                            */
/************************************************************/

#ifndef GenPath_HEADER
#define GenPath_HEADER

#include <string>
#include <vector>
#include "MOOS/libMOOS/Thirdparty/AppCasting/AppCastingMOOSApp.h"
#include "XYPoint.h"

class GenPath : public AppCastingMOOSApp
{
 public:
   GenPath();
   ~GenPath();

 protected: // Standard MOOSApp functions to overload
   bool OnNewMail(MOOSMSG_LIST &NewMail);
   bool Iterate();
   bool OnConnectToServer();
   bool OnStartUp();

 protected: // Standard AppCastingMOOSApp function to overload
   bool buildReport();

 protected:
   void registerVariables();
   void handleMailVisitPoint(const std::string& sval);
   void buildAndPostTour();
   void updateVisited();

 private: // Configuration variables
   std::string m_update_var;     // the Waypoint behavior's "updates" variable
   double      m_visit_radius;   // a point counts as visited within this range (m)

 private: // State variables
   std::vector<XYPoint> m_points;   // received points (the assigned group)
   std::vector<bool>    m_visited;  // parallel to m_points after the tour is built

   bool   m_got_firstpoint;
   bool   m_got_lastpoint;
   bool   m_tour_posted;

   bool   m_nav_received;
   double m_nav_x;
   double m_nav_y;
};

#endif
