#include <iostream>
#include <iomanip>
#include "logging/flight_auto_start.h"
int main() {
  FlightAutoStart detector, mirror;
  unsigned id, ms, flags, last=99999;
  double speed, climb, alt, lat, lon;
  std::cout << "id,ms,candidates,triggered,mean_speed_mps,mean_climb_mps,alt_change_m,displacement_m,valid\n";
  while(std::cin >> id >> ms >> speed >> climb >> alt >> lat >> lon >> flags) {
    if (id != last) {detector.reset(); mirror.reset(); last=id;}
    FlightAutoStart::Sample s;
    s.speedMps=speed; s.climbMps=climb; s.altitudeM=alt; s.latitude=lat; s.longitude=lon;
    s.speedValid=flags&1; s.climbValid=flags&2; s.altitudeValid=flags&4; s.positionValid=flags&8;
    auto reflected=s; reflected.climbMps=-s.climbMps; reflected.altitudeM=-s.altitudeM;
    bool a=detector.update(ms,s), b=mirror.update(ms,reflected);
    if(a!=b || detector.triggered()!=mirror.triggered() || detector.candidates()!=mirror.candidates()) return 2;
    auto w=detector.window();
    unsigned valid=w.speedValid | (w.climbValid<<1) | (w.altitudeValid<<2) | (w.positionValid<<3);
    std::cout << std::setprecision(9) << id << ',' << ms << ',' << unsigned(detector.candidates()) << ',' << unsigned(detector.triggered()) << ',' << w.meanSpeedMps << ',' << w.meanClimbMps << ',' << w.altitudeChangeM << ',' << w.displacementM << ',' << valid << '\n';
  }
}
