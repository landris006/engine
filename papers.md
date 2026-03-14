# Reference Papers

## Microfacet BSDFs / GGX

- **Walter et al., 2007** — *Microfacet Models for Refraction through Rough Surfaces*
  EGSR 2007. Introduces the GGX (Trowbridge-Reitz) normal distribution and Smith G1 for it.

- **Heitz, 2014** — *Understanding the Masking-Shadowing Function in Microfacet-Based BRDFs*
  JCGT 2014. Definitive analysis of Smith G1/G2; derives the height-correlated G2 used here.
  https://jcgt.org/published/0003/02/03/

- **Heitz, 2018** — *Sampling the GGX Distribution of Visible Normals*
  JCGT 2018. VNDF importance sampling — samples only microfacets visible from V, reducing variance at grazing angles.
  https://jcgt.org/published/0007/04/01/
