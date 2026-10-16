/** Phase 12 brief §9: "Provide a proximity legend." The exact three-bucket mapping
 * `LargeConstellationField.tsx` actually uses -- distance from the selected object, split
 * into thirds of the field's own real max distance, never a fixed absolute threshold (a
 * sparse field and a dense field have very different real distance scales). */
export function ProximityLegend() {
  return (
    <div className="proximity-legend">
      <span className="proximity-legend-title">Proximity to selection</span>
      <span className="proximity-legend-item">
        <span className="proximity-swatch proximity-swatch-near" /> Near (closest third)
      </span>
      <span className="proximity-legend-item">
        <span className="proximity-swatch proximity-swatch-medium" /> Medium
      </span>
      <span className="proximity-legend-item">
        <span className="proximity-swatch proximity-swatch-far" /> Far (farthest third)
      </span>
    </div>
  );
}
