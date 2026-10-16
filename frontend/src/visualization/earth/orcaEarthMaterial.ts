/**
 * Earth's surface material: a real day/night terminator, night-side city lights, and an
 * atmospheric limb.
 *
 * ## Why a shader rather than more lights
 *
 * A `MeshStandardMaterial` lit by a directional light gives a terminator, but it cannot do the
 * two things that actually make a rendered planet look like a planet: fade a *different*
 * texture in on the unlit side (city lights), and put a wavelength-shifted rim where the
 * atmosphere scatters light around the limb. Both are cheap here and impossible there, so the
 * surface gets its own shader and the scene keeps exactly one directional light for direction.
 *
 * ## Texture provenance
 *
 * This repository deliberately vendors no image assets — `docs/VISUAL_DESIGN.md` §1 states that
 * outright, and every existing surface is painted procedurally at runtime
 * (`earth/earthTexture.ts`). That stays the default, so the app is complete with no downloads
 * and no licence questions.
 *
 * It is also the honest limit of what procedural painting achieves: a hand-traced coastline set
 * will never look like photography. So this module ALSO looks for real imagery at
 * `public/textures/`, and uses it when present:
 *
 *     frontend/public/textures/earth_day.jpg     surface, equirectangular, longitude -180 at u=0
 *     frontend/public/textures/earth_night.jpg   city lights, same projection (optional)
 *     frontend/public/textures/earth_clouds.jpg  cloud alpha, same projection (optional)
 *
 * NASA's Blue Marble and Black Marble are the obvious sources and are public domain. Nothing is
 * downloaded automatically: a missing file is not an error, it simply falls back to the painted
 * texture, and `usingRealImagery` reports which is in use so the UI can say so rather than
 * implying photography it does not have.
 */

import {
  AdditiveBlending,
  BackSide,
  Color,
  ShaderMaterial,
  SRGBColorSpace,
  TextureLoader,
  Vector3,
  type Texture,
} from 'three';

import { getEarthCloudTexture, getEarthDayTexture } from './earthTexture';

export interface EarthTextureSet {
  readonly day: Texture;
  readonly night: Texture | null;
  readonly clouds: Texture;
  /** True only when a real photographic surface texture was found and loaded. */
  readonly usingRealImagery: boolean;
}

const TEXTURE_BASE = 'textures';

/**
 * Attempts one texture load, resolving to `null` rather than rejecting when the file is absent.
 * A 404 here is an expected, supported configuration — not a failure to report.
 */
function tryLoad(loader: TextureLoader, url: string): Promise<Texture | null> {
  return new Promise((resolve) => {
    loader.load(
      url,
      (texture) => {
        // Colour maps are authored in sRGB; without this they render washed out and too bright.
        texture.colorSpace = SRGBColorSpace;
        resolve(texture);
      },
      undefined,
      () => resolve(null),
    );
  });
}

/**
 * Resolves the texture set, preferring real imagery and falling back to the painted one.
 * Cached: the loaders and canvases are expensive and the result never changes at runtime.
 */
let cached: Promise<EarthTextureSet> | null = null;

export function loadEarthTextures(): Promise<EarthTextureSet> {
  if (cached !== null) return cached;
  const loader = new TextureLoader();
  cached = (async () => {
    const [day, night, clouds] = await Promise.all([
      tryLoad(loader, `${TEXTURE_BASE}/earth_day.jpg`),
      tryLoad(loader, `${TEXTURE_BASE}/earth_night.jpg`),
      tryLoad(loader, `${TEXTURE_BASE}/earth_clouds.jpg`),
    ]);
    return {
      day: day ?? getEarthDayTexture(),
      night,
      clouds: clouds ?? getEarthCloudTexture(),
      usingRealImagery: day !== null,
    };
  })();
  return cached;
}

const SURFACE_VERTEX = /* glsl */ `
  varying vec2 vUv;
  varying vec3 vWorldNormal;
  varying vec3 vWorldPosition;
  void main() {
    vUv = uv;
    // World-space normal, because the sun direction is a world vector and the globe rotates
    // underneath it. Using the object-space normal would freeze the terminator to the texture.
    vWorldNormal = normalize(mat3(modelMatrix) * normal);
    vec4 world = modelMatrix * vec4(position, 1.0);
    vWorldPosition = world.xyz;
    gl_Position = projectionMatrix * viewMatrix * world;
  }
`;

const SURFACE_FRAGMENT = /* glsl */ `
  uniform sampler2D uDayMap;
  uniform sampler2D uNightMap;
  uniform bool uHasNight;
  uniform vec3 uSunDirection;
  uniform vec3 uAtmosphereColor;
  uniform float uNightIntensity;
  uniform float uExposure;
  uniform float uAmbient;
  uniform float uSaturation;
  varying vec2 vUv;
  varying vec3 vWorldNormal;
  varying vec3 vWorldPosition;

  void main() {
    vec3 normal = normalize(vWorldNormal);
    float sun = dot(normal, normalize(uSunDirection));

    // The terminator is a soft band, not a hard edge: Earth's atmosphere scatters light past
    // the geometric boundary, which is why twilight exists.
    float daylight = smoothstep(-0.12, 0.22, sun);

    vec3 dayColor = texture2D(uDayMap, vUv).rgb;

    // Blue Marble is a real photograph, and photographs of Earth are not bright: its average
    // luminance sits near 0.2, so sampling it and multiplying by a lambert term is what
    // produced the "dark blue ball". uExposure is applied BEFORE the shoulder below rather
    // than after, so highlights roll off instead of clipping to white.
    vec3 lit = dayColor * (uAmbient + (1.0 - uAmbient) * daylight) * uExposure;
    lit = lit / (1.0 + lit * 0.28);

    // Brightening a photographic texture costs apparent saturation; a small boost restores the
    // blue of the oceans and the ochre of the land without turning it into a poster.
    float luma = dot(lit, vec3(0.2126, 0.7152, 0.0722));
    lit = mix(vec3(luma), lit, uSaturation);

    // City lights, where a real night map is available. Strongest at full dark and gone well
    // before the terminator, since a lit sky drowns them out long before dawn.
    vec3 nightColor = vec3(0.0);
    if (uHasNight) {
      nightColor = texture2D(uNightMap, vUv).rgb * uNightIntensity;
    }
    float nightMask = 1.0 - smoothstep(-0.25, 0.02, sun);

    vec3 color = lit + nightColor * nightMask;

    // Rim: light scattered around the limb, strongest where the surface turns away from the
    // viewer AND is still sunlit — which is what gives a real planet its bright crescent edge.
    vec3 viewDirection = normalize(cameraPosition - vWorldPosition);
    float fresnel = pow(1.0 - max(dot(viewDirection, normal), 0.0), 3.0);
    color += uAtmosphereColor * fresnel * daylight * 0.85;

    gl_FragColor = vec4(color, 1.0);
    #include <colorspace_fragment>
  }
`;

/**
 * Cloud shell. Lit rather than purely additive: an additive cloud layer brightens the NIGHT side
 * too, laying grey haze over the city lights and destroying the terminator. Clouds are visible
 * only where sunlight falls on them, exactly like the surface beneath them.
 */
const CLOUD_VERTEX = /* glsl */ `
  varying vec2 vUv;
  varying vec3 vWorldNormal;
  void main() {
    vUv = uv;
    vWorldNormal = normalize(mat3(modelMatrix) * normal);
    gl_Position = projectionMatrix * viewMatrix * modelMatrix * vec4(position, 1.0);
  }
`;

const CLOUD_FRAGMENT = /* glsl */ `
  uniform sampler2D uCloudMap;
  uniform vec3 uSunDirection;
  uniform float uOpacity;
  uniform float uExposure;
  varying vec2 vUv;
  varying vec3 vWorldNormal;
  void main() {
    float sun = dot(normalize(vWorldNormal), normalize(uSunDirection));
    float daylight = smoothstep(-0.10, 0.25, sun);
    // The NASA composite is white-on-black, so any channel serves as the density.
    float density = texture2D(uCloudMap, vUv).r;
    float alpha = density * uOpacity * daylight;
    if (alpha < 0.01) discard;
    gl_FragColor = vec4(vec3(uExposure), alpha);
    #include <colorspace_fragment>
  }
`;

export interface EarthSurfaceMaterial extends ShaderMaterial {
  /** Updated each frame from `sun.ts`; kept as a Vector3 so no allocation happens per frame. */
  readonly sunDirection: Vector3;
}

/**
 * Per-theme grading. The planet is the brightest object in either theme — it is the instrument,
 * not the backdrop — but the two need genuinely different handling rather than one being an
 * inversion of the other:
 *
 *  - **dark**: space stays black, so the globe carries all the contrast, and the night side
 *    keeps its city lights, which are the whole reason the terminator reads.
 *  - **light**: the page around the scene is pale, so a globe graded for black surroundings
 *    looks murky against it. Exposure goes higher and the night side is lifted, because a hard
 *    black hemisphere against an off-white page reads as a rendering fault rather than as night.
 */
export interface EarthGrading {
  readonly exposure: number;
  /** Floor under the lambert term: how much of the day texture survives on the unlit side. */
  readonly ambient: number;
  readonly nightIntensity: number;
  readonly saturation: number;
  readonly cloudOpacity: number;
  readonly atmosphereColor: string;
}

export const EARTH_GRADING: Readonly<Record<'dark' | 'light', EarthGrading>> = {
  dark: {
    exposure: 2.15,
    ambient: 0.075,
    nightIntensity: 1.15,
    saturation: 1.18,
    cloudOpacity: 0.62,
    atmosphereColor: '#6fb7ff',
  },
  light: {
    exposure: 2.55,
    ambient: 0.2,
    nightIntensity: 0.55,
    saturation: 1.22,
    cloudOpacity: 0.72,
    atmosphereColor: '#8ac6ff',
  },
};

export function createCloudMaterial(
  textures: EarthTextureSet,
  grading: EarthGrading,
): ShaderMaterial {
  return new ShaderMaterial({
    uniforms: {
      uCloudMap: { value: textures.clouds },
      uSunDirection: { value: new Vector3(1, 0, 0) },
      uOpacity: { value: grading.cloudOpacity },
      uExposure: { value: Math.min(grading.exposure * 0.55, 1.35) },
    },
    vertexShader: CLOUD_VERTEX,
    fragmentShader: CLOUD_FRAGMENT,
    transparent: true,
    depthWrite: false,
  });
}

export function createEarthSurfaceMaterial(
  textures: EarthTextureSet,
  grading: EarthGrading = EARTH_GRADING.dark,
): EarthSurfaceMaterial {
  const sunDirection = new Vector3(1, 0, 0);
  const material = new ShaderMaterial({
    uniforms: {
      uDayMap: { value: textures.day },
      // A sampler uniform must still be bound when unused, or some drivers warn; the day map
      // is reused as a harmless stand-in and `uHasNight` gates every read of it.
      uNightMap: { value: textures.night ?? textures.day },
      uHasNight: { value: textures.night !== null },
      uSunDirection: { value: sunDirection },
      uAtmosphereColor: { value: new Color(grading.atmosphereColor) },
      uNightIntensity: { value: grading.nightIntensity },
      uExposure: { value: grading.exposure },
      uAmbient: { value: grading.ambient },
      uSaturation: { value: grading.saturation },
    },
    vertexShader: SURFACE_VERTEX,
    fragmentShader: SURFACE_FRAGMENT,
  }) as EarthSurfaceMaterial;
  Object.defineProperty(material, 'sunDirection', { value: sunDirection, writable: false });
  return material;
}

const GLOW_VERTEX = /* glsl */ `
  varying vec3 vWorldNormal;
  varying vec3 vWorldPosition;
  void main() {
    vWorldNormal = normalize(mat3(modelMatrix) * normal);
    vec4 world = modelMatrix * vec4(position, 1.0);
    vWorldPosition = world.xyz;
    gl_Position = projectionMatrix * viewMatrix * world;
  }
`;

const GLOW_FRAGMENT = /* glsl */ `
  uniform vec3 uColor;
  uniform vec3 uSunDirection;
  uniform float uIntensity;
  varying vec3 vWorldNormal;
  varying vec3 vWorldPosition;
  void main() {
    vec3 normal = normalize(vWorldNormal);
    vec3 viewDirection = normalize(cameraPosition - vWorldPosition);
    // Back-side shell: the rim is where the shell is most edge-on to the viewer.
    float fresnel = pow(1.0 - abs(dot(viewDirection, normal)), 2.4);
    // Only the daylit limb glows. A halo that ringed the night side evenly would read as a
    // decorative outline rather than as scattered sunlight.
    float sun = smoothstep(-0.35, 0.35, dot(normal, normalize(uSunDirection)));
    gl_FragColor = vec4(uColor, fresnel * sun * uIntensity);
  }
`;

export function createAtmosphereMaterial(color: string, intensity: number): ShaderMaterial {
  return new ShaderMaterial({
    uniforms: {
      uColor: { value: new Color(color) },
      uSunDirection: { value: new Vector3(1, 0, 0) },
      uIntensity: { value: intensity },
    },
    vertexShader: GLOW_VERTEX,
    fragmentShader: GLOW_FRAGMENT,
    transparent: true,
    depthWrite: false,
    side: BackSide,
    blending: AdditiveBlending,
  });
}
