#pragma once
#include "flight.h"

class Kml : public Flight {
 public:
  bool startFlight() override;
  void end(const FlightStats stats, bool showSummary = true) override;

  const String fileNameSuffix() const override { return "kml"; }
  const String desiredFileName() const override;
  void log(unsigned long durationSec) override;
};

const char KMLtrackHeader[] = R"--8<--8<--(<?xml version="1.0" encoding="UTF-8"?>
<kml xmlns="http://www.opengis.net/kml/2.2"
     xmlns:gx="http://www.google.com/kml/ext/2.2">
  <Document>
    <Style id="yellowLineGreenPoly">
    	<IconStyle>
			  <scale>1.3</scale>
			  <Icon>
				  <href>http://maps.google.com/mapfiles/kml/paddle/grn-stars.png</href>
			  </Icon>
			  <hotSpot x="32" y="1" xunits="pixels" yunits="pixels"/>
		  </IconStyle>
      <LineStyle>
        <color>7f00ff7f</color>
        <width>5</width>
      </LineStyle>
      <PolyStyle>
        <color>7fB43C14</color>
      </PolyStyle>
    </Style>
    <Placemark>      
      <styleUrl>#yellowLineGreenPoly</styleUrl>
      <gx:Track>
        <extrude>1</extrude>
        <tessellate>1</tessellate>
        <altitudeMode>absolute</altitudeMode>
)--8<--8<--";

// Coordinates go here in the form of:
// <gx:coord>-122.0822035425683 37.42228990140251 0</gx:coord>
// <when>2026-04-01T12:00:05Z</when>

// Footer is broken into several sub-pieces, so you can insert names and descriptions in between.

const char KMLtrackFooterA[] = R"--8<--8<--(
      </gx:Track>
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
