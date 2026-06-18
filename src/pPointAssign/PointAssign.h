/************************************************************/
/*    NAME: Karolos Geroulanos                              */
/*    ORGN: MIT, Cambridge MA                               */
/*    FILE: PointAssign.h                                   */
/*    DATE: 2026                                            */
/************************************************************/

#ifndef PointAssign_HEADER
#define PointAssign_HEADER

#include <string>
#include <vector>
#include <map>
#include "MOOS/libMOOS/Thirdparty/AppCasting/AppCastingMOOSApp.h"

class PointAssign : public AppCastingMOOSApp
{
 public:
   PointAssign();
   ~PointAssign();

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
   void assignPoint(const std::string& spec, double x, double y);
   void postViewPoint(double x, double y, const std::string& label,
                      const std::string& color);
   std::string colorForIndex(unsigned int index) const;

 private: // Configuration variables
   std::vector<std::string> m_vnames;        // distribute list (one per vname= line)
   bool        m_assign_by_region;           // false=alternating, true=west/east
   double      m_region_split_x;             // x boundary between west and east

 private: // State variables
   unsigned int m_points_total;              // real points received
   unsigned int m_assign_index;              // running counter for alternating
   bool         m_got_firstpoint;
   bool         m_got_lastpoint;
   std::string  m_last_point;                // last real point spec seen
   std::map<std::string, unsigned int> m_vname_count;  // points sent per vehicle
};

#endif
