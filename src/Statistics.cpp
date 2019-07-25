//Copyright (c) 2019 Ultimaker B.V.


#include "Statistics.h"

#include <sstream>
#include <fstream>

#include "utils/logoutput.h"

namespace arachne
{

void Statistics::analyse(Polygons& input, std::vector<std::vector<std::vector<ExtrusionJunction>>>& polygons_per_index, std::vector<std::vector<std::vector<ExtrusionJunction>>>& polylines_per_index, VoronoiQuadrangulation* vq)
{
    this->input = &input;
    this->vq = vq;
    this->polygons_per_index = &polygons_per_index;
    this->polylines_per_index = &polylines_per_index;

    generateAllSegments(polygons_per_index, polylines_per_index);

    
    for (coord_t segment_idx = 0; segment_idx < all_segments.size(); segment_idx++)
    {
        Segment s = all_segments[segment_idx];
        Polygons covered = s.s.toPolygons(false);
        area_covered.add(covered);
        Polygons extruded = s.toPolygons();
        overlaps.add(extruded);
    }
    
    area_covered = area_covered.execute(ClipperLib::pftNonZero);

    overfills = overlaps;
    for (PolygonRef poly : area_covered)
    {
        PolygonRef new_poly = overfills.newPoly();
        for (coord_t point_idx = poly.size() - 1; point_idx >= 0; --point_idx)
        {
            new_poly.add(poly[point_idx]);
        }
    }
    double_overfills = overfills;
    for (PolygonRef poly : area_covered)
    {
        PolygonRef new_poly = double_overfills.newPoly();
        for (coord_t point_idx = poly.size() - 1; point_idx >= 0; --point_idx)
        {
            new_poly.add(poly[point_idx]);
        }
    }
    overfills = overfills.execute(ClipperLib::pftPositive);
    overfills = overfills.unionPolygons(overlaps.xorPolygons(area_covered));
    overfills = overfills.intersection(area_covered);
    overfills = overfills.offset(-5);
    overfills = overfills.offset(10);
    overfills = overfills.offset(-5);

    double_overfills = double_overfills.execute(ClipperLib::pftPositive);
    double_overfills = double_overfills.offset(-5);
    double_overfills = double_overfills.offset(10);
    double_overfills = double_overfills.offset(-5);

    double total_overfill_area = INT2MM2(overfills.area() + double_overfills.area());
    logAlways("Total overfill area: %f mm²\n", total_overfill_area);
    std::vector<PolygonsPart> overfill_areas = overfills.splitIntoParts();
    logAlways("Average area: %f mm² over %d parts\n", total_overfill_area / overfill_areas.size(), overfill_areas.size());

    underfills = input.difference(area_covered);
    underfills = underfills.offset(5);
    underfills = underfills.offset(-10);
    underfills = underfills.offset(5);

    double total_underfill_area = INT2MM2(underfills.area());
    logAlways("Total underfill area: %f mm²\n", total_underfill_area);
    std::vector<PolygonsPart> underfill_areas = underfills.splitIntoParts();
    logAlways("Average area: %f mm² over %d parts\n", total_underfill_area / underfill_areas.size(), underfill_areas.size());

    logAlways("Total target area: %f mm²\n", INT2MM2(input.area()));

    // initialize paths
    for (Segment& segment : all_segments)
    {
        PolygonRef poly = paths.newPoly();
        poly.emplace_back(segment.s.from.p);
        poly.emplace_back(segment.s.to.p);
    }

}

void Statistics::generateAllSegments(std::vector<std::vector<std::vector<ExtrusionJunction>>>& polygons_per_index, std::vector<std::vector<std::vector<ExtrusionJunction>>>& polylines_per_index)
{
    for (std::vector<std::vector<ExtrusionJunction>>& polygons : polygons_per_index)
    {
        for (std::vector<ExtrusionJunction>& polygon : polygons)
        {
            ExtrusionJunction last = polygon.back();
            for (coord_t junction_idx = 0; junction_idx < polygon.size(); junction_idx++)
            {
                ExtrusionJunction& junction = polygon[junction_idx];
                ExtrusionSegment segment(last, junction, false);
                all_segments.emplace_back(segment, false);
                last = junction;
            }
        }
    }
    for (std::vector<std::vector<ExtrusionJunction>>& polylines : polylines_per_index)
    {
        for (std::vector<ExtrusionJunction>& polyline : polylines)
        {
            ExtrusionJunction last = polyline.front();
            for (coord_t junction_idx = 0; junction_idx < polyline.size(); junction_idx++)
            {
                ExtrusionJunction& junction = polyline[junction_idx];
                ExtrusionSegment segment(last, junction, false);
                all_segments.emplace_back(segment, junction_idx == polyline.size() - 1);
                last = junction;
            }
        }
    }
}

void Statistics::visualize()
{
    AABB aabb(*input);

    if (vq)
    {
        std::ostringstream ss;
        ss << "output/" << output_prefix << "_" << filename_base << "_after.svg";
        SVG svg(ss.str(), aabb);
        vq->debugOutput(svg, false, false, true);
        svg.writePolygons(paths, SVG::Color::BLACK, 2);
        
        if (false)
        for (auto polys : *polylines_per_index)
        {
            for (auto poly : polys)
            {
                Point prev = poly.front().p;
                for (ExtrusionJunction& j : poly)
                {
                    svg.writeLine(prev, j.p, SVG::Color::RED, 2);
                    prev = j.p;
                }
            }
        }
        for (auto polylines : *polylines_per_index)
        {
            for (std::vector<ExtrusionJunction>& polyline : polylines)
            {
                svg.writePoint(polyline.front().p, false, 2, SVG::Color::GREEN);
                svg.writePoint(polyline.back().p, false, 2, SVG::Color::BLUE);
            }
        }
    }

    {
        std::ostringstream ss;
        ss << "output/" << output_prefix << "_" << filename_base << "_toolpaths.svg";
        SVG svg(ss.str(), aabb);
        svg.writeAreas(*input, SVG::Color::GRAY, SVG::Color::NONE, 2);
        bool alternate = true;
        for (PolygonRef poly : overlaps)
        {
            svg.writeAreas(poly, alternate? SVG::Color::BLUE : SVG::Color::MAGENTA, SVG::Color::NONE);
            alternate = !alternate;
        }
        svg.writePolygons(paths, SVG::Color::BLACK, 2);
    }

    {
        std::ostringstream ss;
        ss << "output/" << output_prefix << "_" << filename_base << "_widths.svg";
        SVG svg(ss.str(), aabb);
//         svg.writeAreas(*input, SVG::Color::GRAY, SVG::Color::NONE, 2);

        coord_t max_dev = 100;

        // add legend
        auto to_string = [](float v)
        {
            std::ostringstream ss;
            ss << v;
            return ss.str();
        };
        std::vector<Segment> all_segments_plus = all_segments;
        AABB aabb(*input);
        ExtrusionJunction legend_btm(Point(aabb.max.X + 400 + max_dev, aabb.max.Y), 400 - max_dev, 0);
        ExtrusionJunction legend_top(Point(aabb.max.X + 400 + max_dev, aabb.min.Y), 400 + max_dev, 0);
        ExtrusionJunction legend_mid((legend_top.p + legend_btm.p) / 2, (legend_top.w + legend_btm.w) / 2, 0);
        all_segments_plus.emplace_back(ExtrusionSegment(legend_btm, legend_top, true), true);
        Point legend_text_offset(400, 0);
        svg.writeText(legend_top.p + legend_text_offset, to_string(INT2MM(legend_top.w)));
        svg.writeText(legend_btm.p + legend_text_offset, to_string(INT2MM(legend_btm.w)));
        svg.writeText(legend_mid.p + legend_text_offset, to_string(INT2MM(legend_mid.w)));
        svg.writeLine(legend_top.p, legend_top.p + legend_text_offset);
        svg.writeLine(legend_btm.p, legend_btm.p + legend_text_offset);
        svg.writeLine(legend_mid.p, legend_mid.p + legend_text_offset);


        for (const Segment& ss : all_segments_plus)
        {
            for (Segment s : discretize(ss, MM2INT(0.1)))
            {
                coord_t avg_w = (s.s.from.w + s.s.to.w) / 2;
                Point3 gray(64,128,64);
                Point3 red(255,0,0);
                Point3 blue(0,0,255);
                Point3 clr;
                float color_ratio = std::min(1.0, std::abs(avg_w - 400.0) / max_dev);
                color_ratio = sqrt(color_ratio);
                if (avg_w > 400)
                {
                    clr = red * color_ratio + gray * (1.0 - color_ratio );
                }
                else
                {
                    clr = blue * color_ratio + gray * (1.0 - color_ratio );
                }
                s.s.from.w = std::max(static_cast<double>(0), 0.75 * (s.s.from.w + (s.s.from.w - 400) * 2.0));
                s.s.to.w = std::max(static_cast<double>(0), 0.75 * (s.s.to.w + (s.s.to.w - 400) * 2.0));
                Polygons covered = s.toPolygons();
                svg.writeAreas(covered, SVG::ColorObject(clr.x, clr.y, clr.z), SVG::Color::NONE);
            }
        }
//         svg.writePolygons(paths, SVG::Color::BLACK, 1);
    }

    {
        std::ostringstream ss;
        ss << "output/" << output_prefix << "_" << filename_base << "_accuracy.svg";
        SVG svg(ss.str(), aabb);
        svg.writeAreas(*input, SVG::Color::GRAY, SVG::Color::NONE, 3);
        svg.writeAreas(overfills, SVG::Color::RED, SVG::Color::NONE);
        svg.writeAreas(double_overfills, SVG::Color::ORANGE, SVG::Color::NONE);
        svg.writeAreas(underfills, SVG::Color::BLUE, SVG::Color::NONE);
        svg.writePolygons(paths, SVG::Color::BLACK, 1);
    }

    {
        std::ostringstream ss;
        ss << "output/" << output_prefix << "_" << filename_base << "_segments.csv";
        std::ofstream csv(ss.str(), std::ofstream::out | std::ofstream::trunc);
        csv << "from_x; from_y; from_width; to_x; to_y; to_width\n";
        for (const Segment& segment : all_segments)
            csv << segment.s.from.p.X << "; " << segment.s.from.p.Y << "; " << segment.s.from.w << "; " << segment.s.to.p.X << "; " << segment.s.to.p.Y << "; " << segment.s.to.w << '\n';
        csv.close();
    }

}

std::vector<Statistics::Segment> Statistics::discretize(const Segment& segment, coord_t step_size)
{
    ExtrusionSegment extrusion_segment = segment.s;
    Point a = extrusion_segment.from.p;
    Point b = extrusion_segment.to.p;
    Point ab = b - a;
    coord_t ab_length = vSize(ab);
    coord_t step_count = std::max(static_cast<coord_t>(1), (ab_length + step_size / 2) / step_size);
    std::vector<Segment> discretized;
    ExtrusionJunction from = extrusion_segment.from;
    for (coord_t step = 0; step < step_count; step++)
    {
        ExtrusionJunction mid(a + ab * (step + 1) / step_count, extrusion_segment.from.w + (extrusion_segment.to.w - extrusion_segment.from.w) * (step + 1) / step_count, extrusion_segment.from.perimeter_index);
        discretized.emplace_back(ExtrusionSegment(from, mid, segment.s.is_odd), false);
        from = mid;
    }
    discretized.back().is_full = segment.is_full;
    return discretized;
}

} // namespace arachne