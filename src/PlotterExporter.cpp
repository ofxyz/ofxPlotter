#include "PlotterExporter.h"

#include "PlotterStrokeBridge.h"

#include "ofxGCode.hpp"

#include <ofxEnTTKit/src/components/layer_components.h>

#include <sstream>

#include <string>



namespace plotter {



namespace {



ofxGCode buildGCode()

{

    ofxGCode g;

    g.setup(1.0f);

    return g;

}



void addPathsPaper(ofxGCode& g, const std::vector<ofPolyline>& paths)

{

    for (const auto& outline : paths) {

        if (outline.size() < 2) continue;

        std::vector<ofVec2f> pts;

        pts.reserve(outline.size());

        for (const auto& v : outline.getVertices()) {

            pts.push_back({v.x, v.y});

        }

        g.polygon(pts, outline.isClosed());

    }

}



std::vector<ofPolyline> pathsForExport(ImageToPath& engine, ExportOptions& opts) {
    plotproc::StrokeDocument doc = strokeDocumentFromEngine(engine);
    if (opts.runPipeline) {
        const plotproc::PlotPipeline& pipe = opts.pipeline
            ? *opts.pipeline
            : plotproc::PlotPipeline::defaults();
        opts.lastPipelineReport = pipe.runWithReport(doc);
        opts.hasPipelineReport = true;
    }
    if (opts.writeBackToPaths && opts.runPipeline) {
        writeStrokeDocumentToEngine(engine, doc);
        return engine.getPaths();
    }
    return doc.paths;
}



void transformLinesToMachine(ofxGCode& g, const BedView& bed)

{

    const float ox = bed.bed.paperOriginX;

    const float oy = bed.bed.paperOriginY;

    for (auto& ln : g.lines) {

        ln.a.x += ox;

        ln.a.y += oy;

        ln.b.x += ox;

        ln.b.y += oy;

    }

}



std::string buildGCodeString(ofxGCode& g, const PenSettings& pen)

{

    using std::to_string;

    const float safeZ        = pen.penUpZ;

    const float downZ        = pen.penDownZ;

    const float drawF        = pen.drawSpeed;

    const float plungeF      = pen.drawSpeed * 0.5f;

    const bool  slowTravels  = pen.slowTravels;

    const float travelF      = pen.travelSpeed;



    const std::string travelCmd = slowTravels ? "G1" : "G0";

    const std::string travelFStr = slowTravels ? (" F" + to_string((int)travelF)) : "";



    std::vector<std::string> commands;

    commands.push_back("G21 ; mm mode");

    commands.push_back("G90 ; absolute positioning");

    commands.push_back("G0 Z" + ofToString(safeZ, 3) + " ; pen up");



    ofVec2f lastPos(0, 0);

    bool penIsUp   = true;

    bool firstDraw = true;



    for (const auto& line : g.lines) {

        if (line.a != lastPos || penIsUp) {

            if (!penIsUp) {

                commands.push_back("G0 Z" + ofToString(safeZ, 3) + " ; pen up");

                penIsUp = true;

            }

            commands.push_back(travelCmd

                + " X" + ofToString(line.a.x, 3)

                + " Y" + ofToString(line.a.y, 3) + travelFStr);

            commands.push_back("G1 Z" + ofToString(downZ, 3) + " F" + to_string((int)plungeF) + " ; pen down");

            penIsUp   = false;

            firstDraw = true;

        }



        std::string move = "G1 X" + ofToString(line.b.x, 3) + " Y" + ofToString(line.b.y, 3);

        if (firstDraw) { move += " F" + to_string((int)drawF); firstDraw = false; }

        commands.push_back(move);



        lastPos = line.b;

    }



    commands.push_back("G0 Z" + ofToString(safeZ, 3) + " ; pen up");

    if (!g.lines.empty()) {

        commands.push_back(travelCmd

            + " X" + ofToString(g.lines.front().a.x, 3)

            + " Y" + ofToString(g.lines.front().a.y, 3) + travelFStr + " ; park");

    }

    commands.push_back("M2 ; end program");



    std::string result;

    result.reserve(commands.size() * 24);

    for (const auto& cmd : commands) result += cmd + "\n";

    return result;

}



std::string commandsToString(const std::vector<std::string>& commands)

{

    std::string result;

    result.reserve(commands.size() * 24);

    for (const auto& cmd : commands) result += cmd + "\n";

    return result;

}



std::string exportWithOptions(ofxGCode& g,

                              const ImageToPath& engine,

                              const std::string& header,

                              const ExportOptions& opts)

{

    const bool useMachine = opts.prefs != nullptr;

    if (useMachine) {

        const BedView bed = BedView::fromPrefs(*opts.prefs);

        transformLinesToMachine(g, bed);



        if (opts.zones && !opts.zones->injectionRules.empty()) {

            const auto breaks = computeInjectionBreaks(

                g.lines, opts.zones->injectionRules, false);

            if (!breaks.empty()) {

                const auto base = buildGCodeString(g, engine.pen);

                std::vector<std::string> baseCmds;

                {

                    std::istringstream ss(base);

                    std::string line;

                    while (std::getline(ss, line)) {

                        if (!line.empty() && line.back() == '\r') line.pop_back();

                        if (!line.empty()) baseCmds.push_back(line);

                    }

                }

                const auto injected = applyInjectionToCommands(

                    baseCmds, g.lines, breaks, opts.zones->injectionRules,

                    *opts.zones, engine.pen, opts.prefs->envelope);

                return header + commandsToString(injected);

            }

        }

    }



    return header + buildGCodeString(g, engine.pen);

}



} // namespace



std::string toGCode(ImageToPath& engine, ExportOptions& opts)

{

    const std::vector<ofPolyline> exportPaths = pathsForExport(engine, opts);



    ofxGCode g = buildGCode();

    addPathsPaper(g, exportPaths);



    std::string header;

    header += "; Generated by ofxPlotter (via ofxGCode)\n";

    header += "; Paths: "    + std::to_string(exportPaths.size())

            + ", Distance: " + std::to_string((int)engine.totalDistance) + " mm\n";

    if (opts.hasPipelineReport) {

        header += "; Travel before pipeline: "

                + ofToString(opts.lastPipelineReport.initial.travelLengthMM, 1) + " mm\n";

        header += "; Travel after pipeline: "

                + ofToString(opts.lastPipelineReport.final.travelLengthMM, 1) + " mm\n";

    }

    header += "; Estimated time: " + std::to_string((int)(engine.estimatedTime / 60)) + " min\n";

    if (engine.pen.slowTravels)

        header += "; slowTravels: G1 F" + std::to_string((int)engine.pen.travelSpeed) + "\n";

    if (opts.prefs)

        header += "; Machine coords (paper origin "

                + std::to_string(opts.prefs->bed.paperOriginX) + ","

                + std::to_string(opts.prefs->bed.paperOriginY) + " mm)\n";

    if (!opts.externalCommand.empty()) {
        header += "; externalCommand (optional batch hook): " + opts.externalCommand + "\n";
        ofLogVerbose("PlotterExporter")
            << "externalCommand is not executed in-process; run manually: "
            << opts.externalCommand;
    }
    header += "\n";



    return exportWithOptions(g, engine, header, opts);

}



std::string toGCodeForLayer(ImageToPath& engine,

                            entt::entity layerEntity,

                            ExportOptions& opts)

{

    if (!engine.registry.valid(layerEntity)) return "";



    const auto& layer = engine.registry.get<plotter::paths_component>(layerEntity);

    const auto& stats = engine.registry.get<plotter::toolpath_stats_component>(layerEntity);

    const auto& lc    = engine.registry.get<ecs::layer_component>(layerEntity);



    plotproc::StrokeDocument doc;

    for (size_t pi = 0; pi < layer.paths.size(); ++pi) {

        for (const auto& outline : layer.paths[pi].getOutline()) {

            if (outline.size() < 2) continue;

            doc.paths.push_back(outline);

            plotproc::StrokeMeta meta;

            meta.closed = outline.isClosed();

            meta.layerId = 0;

            meta.color = (pi < layer.pathColors.size()) ? layer.pathColors[pi] : lc.color;

            doc.meta.push_back(meta);

        }

    }



    if (opts.runPipeline) {

        const plotproc::PlotPipeline& pipe = opts.pipeline

            ? *opts.pipeline

            : plotproc::PlotPipeline::defaults();

        opts.lastPipelineReport = pipe.runWithReport(doc);

        opts.hasPipelineReport = true;

    }



    ofxGCode g = buildGCode();

    addPathsPaper(g, doc.paths);



    std::string header;

    header += "; Generated by ofxPlotter (via ofxGCode) - Layer: " + lc.name + "\n";

    header += "; Paths: "    + std::to_string(stats.totalPaths)

            + ", Distance: " + std::to_string((int)stats.totalDistance) + " mm\n";

    header += "; Estimated time: " + std::to_string((int)(stats.estimatedTime / 60)) + " min\n";

    if (engine.pen.slowTravels)

        header += "; slowTravels: G1 F" + std::to_string((int)engine.pen.travelSpeed) + "\n";

    header += "\n";



    return exportWithOptions(g, engine, header, opts);

}



} // namespace plotter

