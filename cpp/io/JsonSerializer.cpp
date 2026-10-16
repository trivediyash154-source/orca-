#include "JsonSerializer.hpp"

namespace orca::io {

namespace {

JsonValue toJson(const PositionView& p) {
    return JsonValue::object().set("x", p.x).set("y", p.y).set("z", p.z);
}

JsonValue toJson(const OrbitView& o) {
    return JsonValue::object()
        .set("altitudeKm", o.altitudeKm)
        .set("inclinationDeg", o.inclinationDeg)
        .set("raanDeg", o.raanDeg)
        .set("phaseDeg", o.phaseDeg)
        .set("band", o.band);
}

JsonValue toJson(const HealthView& h) {
    return JsonValue::object()
        .set("power", h.power)
        .set("propulsion", h.propulsion)
        .set("comms", h.comms)
        .set("thermal", h.thermal);
}

JsonValue optionalString(const std::optional<std::string>& s) {
    return s ? JsonValue(*s) : JsonValue(nullptr);
}

JsonValue toJson(const SatelliteView& s) {
    return JsonValue::object()
        .set("id", s.id)
        .set("name", s.name)
        .set("status", s.status)
        .set("position", toJson(s.position))
        .set("orbit", toJson(s.orbit))
        .set("fuelFraction", s.fuelFraction)
        .set("health", toJson(s.health))
        .set("activeEmergencyId", optionalString(s.activeEmergencyId));
}

JsonValue toJson(const VehicleView& v) {
    return JsonValue::object()
        .set("id", v.id)
        .set("name", v.name)
        .set("status", v.status)
        .set("position", toJson(v.position))
        .set("orbit", toJson(v.orbit))
        .set("deltaVBudgetKms", v.deltaVBudgetKms)
        .set("deltaVRemainingKms", v.deltaVRemainingKms)
        .set("fuelFraction", v.fuelFraction)
        .set("health", toJson(v.health))
        .set("activeMissionId", optionalString(v.activeMissionId));
}

JsonValue toJson(const DebrisView& d) {
    return JsonValue::object()
        .set("id", d.id)
        .set("position", toJson(d.position))
        .set("hazardRadiusKm", d.hazardRadiusKm)
        .set("densityFactor", d.densityFactor)
        .set("detected", d.detected);
}

JsonValue toJson(const EmergencyView& e) {
    return JsonValue::object()
        .set("id", e.id)
        .set("subjectId", e.subjectId)
        .set("kind", e.kind)
        .set("severity", e.severity)
        .set("state", e.state)
        .set("detectedAtSec", e.detectedAtSec)
        .set("deadlineSec", e.deadlineSec)
        .set("remainingWindowSec", e.remainingWindowSec)
        .set("urgency", e.urgency);
}

JsonValue toJson(const MissionLegView& leg) {
    return JsonValue::object()
        .set("fromEntityId", leg.fromEntityId)
        .set("toEntityId", leg.toEntityId)
        .set("fromPosition", toJson(leg.fromPosition))
        .set("toPosition", toJson(leg.toPosition))
        .set("departureTimeSec", leg.departureTimeSec)
        .set("arrivalTimeSec", leg.arrivalTimeSec)
        .set("deltaVKms", leg.deltaVKms)
        .set("status", leg.status);
}

JsonValue toJson(const ObjectiveVectorView& v) {
    return JsonValue::object()
        .set("time", v.time)
        .set("fuel", v.fuel)
        .set("risk", v.risk)
        .set("distance", v.distance);
}

JsonValue toJson(const ParetoPointView& p) {
    return JsonValue::object()
        .set("planId", p.planId)
        .set("cost", toJson(p.cost))
        .set("dominated", p.dominated)
        .set("recommended", p.recommended);
}

JsonValue toJson(const ParetoHistoryEntryView& entry) {
    JsonValue points = JsonValue::array();
    for (const ParetoPointView& p : entry.points) points.push(toJson(p));
    return JsonValue::object()
        .set("planningPass", entry.planningPass)
        .set("timestampSec", entry.timestampSec)
        .set("points", std::move(points));
}

JsonValue toJson(const MissionView& m) {
    JsonValue legs = JsonValue::array();
    for (const MissionLegView& leg : m.legs) legs.push(toJson(leg));
    JsonValue front = JsonValue::array();
    for (const ParetoPointView& p : m.paretoFront) front.push(toJson(p));
    JsonValue history = JsonValue::array();
    for (const ParetoHistoryEntryView& entry : m.paretoHistory) history.push(toJson(entry));

    return JsonValue::object()
        .set("id", m.id)
        .set("vehicleId", m.vehicleId)
        .set("emergencyId", m.emergencyId)
        .set("state", m.state)
        .set("currentLeg", m.currentLeg)
        .set("progressOnLeg", m.progressOnLeg)
        .set("deltaVSpentKms", m.deltaVSpentKms)
        .set("replanCount", m.replanCount)
        .set("startedAtSec", m.startedAtSec)
        .set("completedAtSec", m.completedAtSec ? JsonValue(*m.completedAtSec) : JsonValue(nullptr))
        .set("legs", std::move(legs))
        .set("paretoFront", std::move(front))
        .set("paretoHistory", std::move(history))
        .set("explanation", m.explanation ? JsonValue(*m.explanation) : JsonValue(nullptr))
        .set("activeRescoreProfile",
            m.activeRescoreProfile ? JsonValue(*m.activeRescoreProfile) : JsonValue(nullptr));
}

JsonValue toJson(const EventView& e) {
    return JsonValue::object()
        .set("id", e.id)
        .set("kind", e.kind)
        .set("timestampSec", e.timestampSec)
        .set("satelliteId", optionalString(e.satelliteId))
        .set("vehicleId", optionalString(e.vehicleId))
        .set("emergencyId", optionalString(e.emergencyId))
        .set("missionId", optionalString(e.missionId))
        .set("debrisId", optionalString(e.debrisId))
        .set("detail", e.detail);
}

JsonValue toJson(const MetricsView& m) {
    return JsonValue::object()
        .set("ticks", m.ticks)
        .set("eventsProcessed", m.eventsProcessed)
        .set("missionsUpdated", m.missionsUpdated)
        .set("replansTriggered", m.replansTriggered)
        .set("replansCompleted", m.replansCompleted)
        .set("missionSuccesses", m.missionSuccesses)
        .set("missionFailures", m.missionFailures)
        .set("missionAborts", m.missionAborts);
}

template <typename T>
JsonValue optionalValue(const std::optional<T>& v) {
    return v ? JsonValue(*v) : JsonValue(nullptr);
}

JsonValue toJson(const AlgorithmRunView& r) {
    return JsonValue::object()
        .set("algorithm", r.algorithm)
        .set("timestampSec", r.timestampSec)
        .set("found", r.found)
        .set("arrivalTimeSec", optionalValue(r.arrivalTimeSec))
        .set("executionTimeMs", r.executionTimeMs)
        .set("nodesExpanded", optionalValue(r.nodesExpanded))
        .set("edgesEvaluated", optionalValue(r.edgesEvaluated))
        .set("relaxationsSucceeded", optionalValue(r.relaxationsSucceeded))
        .set("relaxationsRejected", optionalValue(r.relaxationsRejected))
        .set("priorityQueuePushes", optionalValue(r.priorityQueuePushes))
        .set("priorityQueuePops", optionalValue(r.priorityQueuePops))
        .set("staleEntriesDiscarded", optionalValue(r.staleEntriesDiscarded))
        .set("heuristicEvaluations", optionalValue(r.heuristicEvaluations))
        .set("labelsGenerated", optionalValue(r.labelsGenerated))
        .set("labelsInserted", optionalValue(r.labelsInserted))
        .set("labelsDominated", optionalValue(r.labelsDominated))
        .set("labelsPruned", optionalValue(r.labelsPruned))
        .set("labelsExpanded", optionalValue(r.labelsExpanded))
        .set("dominanceComparisons", optionalValue(r.dominanceComparisons))
        .set("queuePushes", optionalValue(r.queuePushes))
        .set("queuePops", optionalValue(r.queuePops))
        .set("maximumLabelsAtNode", optionalValue(r.maximumLabelsAtNode))
        .set("truncated", optionalValue(r.truncated))
        .set("vehicleCount", optionalValue(r.vehicleCount))
        .set("emergencyCount", optionalValue(r.emergencyCount))
        .set("candidatePairsEvaluated", optionalValue(r.candidatePairsEvaluated))
        .set("feasibleCandidatePairs", optionalValue(r.feasibleCandidatePairs))
        .set("assignedPairs", optionalValue(r.assignedPairs))
        .set("solverMatrixDimension", optionalValue(r.solverMatrixDimension))
        .set("solverOperationCount", optionalValue(r.solverOperationCount));
}

JsonValue toJson(const GraphNodeView& n) {
    return JsonValue::object()
        .set("id", n.id)
        .set("kind", n.kind)
        .set("entityId", n.entityId)
        .set("orbit", toJson(n.orbit));
}

JsonValue toJson(const GraphEdgeView& e) {
    return JsonValue::object()
        .set("id", e.id)
        .set("from", e.from)
        .set("to", e.to)
        .set("cost", toJson(e.cost))
        .set("feasible", e.feasible);
}

template <typename T>
JsonValue toJsonArray(const std::vector<T>& items) {
    JsonValue arr = JsonValue::array();
    for (const T& item : items) arr.push(toJson(item));
    return arr;
}

JsonValue toJson(const GraphView& g) {
    return JsonValue::object().set("nodes", toJsonArray(g.nodes)).set("edges", toJsonArray(g.edges));
}

}  // namespace

JsonValue toJson(const SimulationSnapshot& snapshot) {
    return JsonValue::object()
        .set("type", "snapshot")
        .set("schemaVersion", snapshot.schemaVersion)
        .set("scenarioName", snapshot.scenarioName)
        .set("simulationTimeSec", snapshot.simulationTimeSec)
        .set("simulationStatus", snapshot.simulationStatus)
        .set("speedMultiplier", snapshot.speedMultiplier)
        .set("satellites", toJsonArray(snapshot.satellites))
        .set("rescueVehicles", toJsonArray(snapshot.rescueVehicles))
        .set("debris", toJsonArray(snapshot.debris))
        .set("emergencies", toJsonArray(snapshot.emergencies))
        .set("missions", toJsonArray(snapshot.missions))
        .set("recentEvents", toJsonArray(snapshot.recentEvents))
        .set("metrics", toJson(snapshot.metrics))
        .set("graph", toJson(snapshot.graph))
        .set("algorithmRuns", toJsonArray(snapshot.algorithmRuns));
}

std::string serializeSnapshotMessage(const SimulationSnapshot& snapshot,
                                     std::optional<std::uint32_t> reqId) {
    JsonValue msg = toJson(snapshot);
    if (reqId) {
        msg.set("reqId", *reqId);
    }
    return msg.dump();
}

std::string serializeReadyMessage(const std::string& engineVersion, int schemaVersion,
                                  const std::vector<std::string>& capabilities) {
    JsonValue caps = JsonValue::array();
    for (const std::string& c : capabilities) caps.push(c);
    return JsonValue::object()
        .set("type", "ready")
        .set("schemaVersion", schemaVersion)
        .set("engineVersion", engineVersion)
        .set("capabilities", std::move(caps))
        .dump();
}

std::string serializeErrorMessage(const std::string& code, const std::string& message,
                                  std::optional<std::uint32_t> reqId) {
    JsonValue msg = JsonValue::object().set("type", "error").set("code", code).set("message", message);
    if (reqId) {
        msg.set("reqId", *reqId);
    }
    return msg.dump();
}

}  // namespace orca::io
