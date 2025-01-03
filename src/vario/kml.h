#ifndef kml_h
#define kml_h

const char KMLtrackHeader[] = R"--8<--8<--(<?xml version="1.0" encoding="UTF-8"?>
<kml xmlns="http://www.opengis.net/kml/2.2">
  <Document>
    <Style id="yellowLineGreenPoly">
      <LineStyle>
        <color>7fF0A014</color>
        <width>4</width>
      </LineStyle>
      <PolyStyle>
        <color>7fB43C14</color>
      </PolyStyle>
    </Style>
    <Placemark>      
      <styleUrl>#yellowLineGreenPoly</styleUrl>
      <LineString>
        <extrude>1</extrude>
        <tessellate>1</tessellate>
        <altitudeMode>absolute</altitudeMode>
        <coordinates>
)--8<--8<--";

// Coordinates go here in the form of:
// LONG_DEC, LAT_DEC, ALT
// LONG_DEC, LAT_DEC, ALT
// .. etc.  Note no comma after ALT

// Footer is broken into several sub-pieces, so you can insert names and descriptions in between.

const char KMLtrackFooterA[] = R"--8<--8<--(        </coordinates>
      </LineString>
      <name>)--8<--8<--";

// Print Track Name Here

const char KMLtrackFooterB[] = R"--8<--8<--(</name>
      <description>)--8<--8<--";

// Print Track Description Here

const char KMLtrackFooterC[] = R"--8<--8<--(</description>
    </Placemark>
    <name>)--8<--8<--";

// Print File Name Here

const char KMLtrackFooterD[] = R"--8<--8<--(</name>
    <description>)--8<--8<--";

// Print File Description Here

const char KMLtrackFooterE[] = R"--8<--8<--(</description>
  </Document>
</kml>)--8<--8<--";

#endif