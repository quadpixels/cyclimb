#include "lssintersect.h"

bool g_use_do_hit_old{ false };

bool Sphere::Hit(const Ray& r, float t_min, float t_max, HitRecord& rec) const {
  glm::vec3 oc = r.origin - center;
  auto a = glm::dot(r.direction, r.direction);
  auto half_b = glm::dot(oc, r.direction);
  auto c = glm::dot(oc, oc) - radius * radius;

  auto discriminant = half_b * half_b - a * c;
  if (discriminant < 0)
    return false;
  auto sqrtd = sqrt(discriminant);

  // Find the nearest root that lies in the acceptable range.
  auto cand0 = (-half_b - sqrtd) / a;
  auto cand1 = (-half_b + sqrtd) / a;
  if (use_far_hitpoint) {
    std::swap(cand0, cand1);
  }
  auto root = cand0;
  if (root < t_min || t_max < root) {
    root = cand1;
    if (root < t_min || t_max < root)
      return false;
  }

  rec.t = root;
  rec.p = r.at(rec.t);
  glm::vec3 out = (rec.p - center) * (1.0f / radius);
  rec.SetFaceNormal(r, out);
  return true;
}

struct Line3 {
  glm::vec3 P, U;
};

struct Cone3 {
  glm::vec3 V, D;
  float cosAngleSqr;
  float hmin, hmax;
  bool isFinite;
  char HeightInRange(float h) const {
    if (h < hmin) return -1;
    else if (!isFinite && h > hmax) return 1;
    else return 0;
  }
};

enum ConeIntersectionType {
  NONE,
  POINT,
  SEGMENT,
  RAY_POSITIVE,
  RAY_NEGATIVE
};

static ConeIntersectionType SetEmpty(float t[2]) {
  t[0] = std::nanf("1");
  t[1] = std::nanf("1");
  return ConeIntersectionType::NONE;
}

static ConeIntersectionType SetPoint(float t0, float t[2]) {
  t[0] = t0;
  t[1] = std::nanf("1");
  return ConeIntersectionType::POINT;
}

static ConeIntersectionType SetSegment(float t0, float t1, float t[2]) {
  t[0] = t0; t[1] = t1;
  return ConeIntersectionType::SEGMENT;
}

static int FindIntersection(float u0, float u1, float v0, float v1, float overlap[2]) {
  int numValid{};
  if (u1 < v0 || v1 < u0) {
    numValid = 0;
  }
  else if (v0 < u1) {
    if (u0 < v1) {
      overlap[0] = (u0 < v0 ? v0 : u0);
      overlap[1] = (u1 > v1 ? v1 : u1);
      if (overlap[0] < overlap[1]) {
        numValid = 2;
      }
      else {
        numValid = 1;
      }
    }
    else {  // u0 == v1
      overlap[0] = overlap[1] = u0;
      numValid = 1;
    }
  }
  else {  // u1 == v0
    overlap[0] = overlap[1] = v0;
    numValid = 1;
  }
  return numValid;
}

static int FindIntersection(float u0, float u1, float v, float overlap[2]) {
  int numValid{};
  if (u1 > v) {
    numValid = 2;
    overlap[0] = std::max(u0, v);
    overlap[1] = u1;
  }
  else if (u1 == v) {
    numValid = 1;
    overlap[0] = v;
  }
  else {  // u1 < v
    numValid = 0;
  }
  return numValid;
}

static ConeIntersectionType SetPointClamp(float t0, float h0, const Cone3& cone, float t[2], char range_end[2]) {
  char re = cone.HeightInRange(h0);
  if (h0 <= cone.hmin) {
    range_end[0] = -1;
  }
  else if (h0 >= cone.hmax) {
    range_end[0] = 1;
  }

  if (re) {
    return SetPoint(t0, t);
  }
  else {
    return SetEmpty(t);
  }
}

static ConeIntersectionType SetSegmentClamp(float t0, float t1, float h0, float h1, float DdU, float DdPmV, const Cone3& cone, float t[2], char range_end[2]) {
  range_end[0] = range_end[1] = 0;
  if (h1 > h0) {
    int numValid{};
    float overlap[2];
    if (cone.isFinite) {
      numValid = FindIntersection(h0, h1, cone.hmin, cone.hmax, overlap);
    }
    else {
      numValid = FindIntersection(h0, h1, cone.hmin, overlap);
    }

    if (h0 <= cone.hmin) { range_end[0] = -1; }
    if (h1 >= cone.hmax) { range_end[1] = 1; }

    if (numValid == 2) {
      float t0 = (overlap[0] - DdPmV) / DdU, t1 = (overlap[1] - DdPmV) / DdU;
      return SetSegment(t0, t1, t);
    }
    else if (numValid == 1) {
      float t0 = (overlap[0] - DdPmV) / DdU;
      return SetPoint(t0, t);
    }
    else {
      return SetEmpty(t);
    }
  }
  else {  // h1 == h0
    char range_end = cone.HeightInRange(h0);
    if (range_end) {
      return SetSegment(t0, t1, t);
    }
    else {
      return SetEmpty(t);
    }
  }
  assert(0);
}

static ConeIntersectionType SetRayPositive(float t0, float t[2]) {
  t[0] = t0;
  t[1] = std::nanf("1");
  return ConeIntersectionType::RAY_POSITIVE;
}

static ConeIntersectionType SetRayNegative(float t1, float t[2]) {
  t[0] = std::nanf("1");
  t[1] = t1;
  return ConeIntersectionType::RAY_NEGATIVE;
}

static ConeIntersectionType SetRayClamp(float h, float DdU, float DdPmV, const Cone3& cone, float t[2]) {
  if (cone.isFinite) {
    float overlap[2];
    int numValid = FindIntersection(cone.hmin, cone.hmax, h, overlap);
    if (numValid == 2) {
      return SetSegment((overlap[0] - DdPmV) / DdU, (overlap[1] - DdPmV) / DdU, t);
    }
    else if (numValid == 1) {
      return SetPoint((overlap[0] - DdPmV) / DdU, t);
    }
    else {
      return SetEmpty(t);
    }
  }
  else {
    return SetRayPositive((std::max(cone.hmin, h) - DdPmV) / DdU, t);
  }
}

static ConeIntersectionType CaseC2NotZeroDiscrNeg(float t[2]) {
  return SetEmpty(t);
}

static ConeIntersectionType CaseC2NotZeroDiscrPos(float c1, float c2, float discr, float DdU, float DdPmV, const Cone3& cone, float t[2], char range_ends[2]) {
  float x = -c1 / c2;
  float y = (c2 > 0 ? 1 / c2 : -1 / c2);
  float t0 = x - y * sqrt(discr), t1 = x + y * sqrt(discr);
  float h0 = t0 * DdU + DdPmV, h1 = t1 * DdU + DdPmV;
  if (h0 >= 0) {
    return SetSegmentClamp(t0, t1, h0, h1, DdU, DdPmV, cone, t, range_ends);
  }
  else if (h1 <= 0) {
    return SetEmpty(t);
  }
  else {
    return SetRayClamp(h1, DdU, DdPmV, cone, t);
  }
}

static ConeIntersectionType CaseC2NotZeroDiscrZero(float c1, float c2, float UdU, float UdPmV, float DdU, float DdPmV, const Cone3& cone, float tt[2], char range_end[2]) {
  float t = -c1 / c2;
  if (t * UdU + UdPmV == 0) {
    if (c2 < 0) {
      float h = 0;
      return SetPointClamp(t, h, cone, tt, range_end);
    }
    else {
      float h = 0;
      return SetRayClamp(h, DdU, DdPmV, cone, tt);
    }
  }
  else {
    float h = t * DdU + DdPmV;
    if (h >= 0) {
      return SetPointClamp(t, h, cone, tt, range_end);
    }
    else {
      return SetEmpty(tt);
    }
  }
}

static ConeIntersectionType CaseC2ZeroC1NotZero(float c0, float c1, float DdU, float DdPmV, const Cone3& cone, float tt[2]) {
  float t = -c0 / (2 * c1);
  float h = t * DdU + DdPmV;
  if (h > 0) {
    return SetRayClamp(h, DdU, DdPmV, cone, tt);
  }
  else {
    return SetEmpty(tt);
  }
}

static ConeIntersectionType CaseC2ZeroC1Zero(float c0, float UdU, float UdPmV, float DdU, float DdPmV, const Cone3& cone, float tt[2]) {
  if (c0 != 0) {
    return SetEmpty(tt);
  }
  else {
    float t = -UdPmV / UdU;
    float h = t * DdU + DdPmV;
    return SetRayClamp(h, DdU, DdPmV, cone, tt);
  }
}

static ConeIntersectionType ConeDoQuerySpecial(const glm::vec3& P, const glm::vec3& U, const Cone3& cone, float t[2], char range_ends[2]) {
  glm::vec3 PmV = P - cone.V;
  float DdU = glm::dot(cone.D, U);
  float UdU = glm::dot(U, U);
  float DdPmV = glm::dot(cone.D, PmV);
  float UdPmV = glm::dot(U, PmV);
  float PmVdPmV = glm::dot(PmV, PmV);
  float c2 = DdU * DdU - cone.cosAngleSqr * UdU;
  float c1 = DdU * DdPmV - cone.cosAngleSqr * UdPmV;
  float c0 = DdPmV * DdPmV - cone.cosAngleSqr * PmVdPmV;

  if (c2 != 0) {
    float discr = c1 * c1 - c0 * c2;
    if (discr < 0) {
      return CaseC2NotZeroDiscrNeg(t);
    }
    else if (discr > 0) {
      return CaseC2NotZeroDiscrPos(c1, c2, discr, DdU, DdPmV, cone, t, range_ends);
    }
    else {
      return CaseC2NotZeroDiscrZero(c1, c2, UdU, UdPmV, DdU, DdPmV, cone, t, range_ends);
    }
  }
  else if (c1 != 0) {
    return CaseC2ZeroC1NotZero(c0, c1, DdU, DdPmV, cone, t);
  }
  else {
    return CaseC2ZeroC1Zero(c0, UdU, UdPmV, DdU, DdPmV, cone, t);
  }
  assert(false);
}

// Copied from https://www.geometrictools.com/Documentation/IntersectionLineCone.pdf
static ConeIntersectionType ConeDoQuery(const glm::vec3& P, const glm::vec3& U, const Cone3& cone, float t[2], char range_ends[2]) {
  ConeIntersectionType intersectionType{};
  float DdU = glm::dot(cone.D, U);
  if (DdU > 0) {
    intersectionType = ConeDoQuerySpecial(P, U, cone, t, range_ends);
  }
  else {
    intersectionType = ConeDoQuerySpecial(P, -U, cone, t, range_ends);
    t[0] = -t[0];
    t[1] = -t[1];
    std::swap(t[0], t[1]);
    std::swap(range_ends[0], range_ends[1]);
    if (intersectionType == ConeIntersectionType::RAY_POSITIVE) {
      intersectionType = ConeIntersectionType::RAY_NEGATIVE;
    }
  }
  return intersectionType;
}

glm::vec3 ConeComputeNormal(const Cone3& cone, glm::vec3& p, char range_end, bool is_inside) {
  glm::vec3 invalid(std::nanf("1"), std::nanf("1"), std::nanf("1"));
  if (range_end == 1) {
    if (is_inside) {
      p = invalid;
      return invalid;
    }
    else {
      return glm::normalize(cone.D);
    }
  }
  else if (range_end == -1) {
    if (is_inside) {
      p = invalid;
      return invalid;
    }
    else {
      return glm::normalize(cone.D) * -1.0f;
    }
  }
  glm::vec3 vp = p - cone.V;
  glm::vec3 x = glm::cross(vp, cone.D);
  return glm::normalize(glm::cross(vp, x));
}

void ConeComputePoints(ConeIntersectionType intersectionType, const glm::vec3& origin, const glm::vec3& direction, const Cone3& cone, float t[2], glm::vec3 P[2], glm::vec3 N[2], char range_ends[2], bool is_inside) {
  glm::vec3 invalid(std::nanf("1"), std::nanf("1"), std::nanf("1"));
  switch (intersectionType) {
  case ConeIntersectionType::NONE: {
    P[0] = invalid;
    P[1] = invalid;
    N[0] = invalid;
    N[1] = invalid;
    break;
  }
  case ConeIntersectionType::POINT: {
    P[0] = origin + t[0] * direction;
    P[1] = invalid;
    N[0] = ConeComputeNormal(cone, P[0], range_ends[0], is_inside);
    N[1] = invalid;
    break;
  }
  case ConeIntersectionType::SEGMENT: {
    P[0] = origin + t[0] * direction;
    P[1] = origin + t[1] * direction;
    N[0] = ConeComputeNormal(cone, P[0], range_ends[0], is_inside);
    N[1] = ConeComputeNormal(cone, P[1], range_ends[1], is_inside);
    break;
  }
  case ConeIntersectionType::RAY_POSITIVE: {
    P[0] = origin + t[0] * direction;
    P[1] = invalid;
    N[0] = ConeComputeNormal(cone, P[0], range_ends[0], is_inside);
    N[1] = invalid;
    break;
  }
  case ConeIntersectionType::RAY_NEGATIVE: {
    P[0] = invalid;
    P[1] = origin + t[1] * direction;
    N[0] = invalid;
    N[1] = ConeComputeNormal(cone, P[1], range_ends[1], is_inside);
    break;
  }
  }
}

bool LinearSweptSphere::HitSpheres(const Ray& r, float t_min, float t_max, HitRecord& rec, bool is_inside) const {
  Sphere s0(c0, r0), s1(c1, r1);
  s0.use_far_hitpoint = is_inside;
  s1.use_far_hitpoint = is_inside;
  HitRecord tmp_rec{};
  if (s0.Hit(r, t_min, t_max, tmp_rec)) {
    rec = tmp_rec;
  }
  if (s1.Hit(r, t_min, t_max, tmp_rec)) {
    if (rec.t > tmp_rec.t) {
      rec = tmp_rec;
    }
  }
  return true;
}

bool LinearSweptSphere::do_hit_old(const Ray& r, float t_min, float t_max,
  HitRecord& rec) const {
  glm::vec3 c0c1 = c1 - c0;
  glm::vec3 c0c1_dir = glm::normalize(c0c1);
  const float len_c0c1 = sqrtf(glm::dot(c0c1, c0c1));
  const float rdiff = r0 - r1;
  assert(rdiff != 0);  // Cylinder
  const float len_tot = len_c0c1 / rdiff * r0;  // c0 to V

  glm::vec3 V{};
  V = c0 + c0c1_dir * len_tot;  // Center of cone
  glm::vec3 D = c0c1_dir * -1.0f;
  const float cosAngleSqr = 1 - rdiff * rdiff / len_c0c1 / len_c0c1;
  const float sinAngle = sqrtf(1.0f - cosAngleSqr);

  float hmax = len_tot - sinAngle * r0;
  float hmin = len_tot - len_c0c1 - sinAngle * r1;
  if (rdiff < 0) {
    hmin *= -1; hmax *= -1;
    std::swap(hmin, hmax);
    D *= -1.0f;
  }

  bool is_inside = false;
  if (glm::dot(r.origin - c0, r.origin - c0) <= r0 * r0) is_inside = true;
  if (!is_inside) {
    if (glm::dot(r.origin - c1, r.origin - c1) <= r1 * r1) is_inside = true;
  }
  if (!is_inside) {
    float dp = glm::dot(r.origin - V, D);
    if (dp >= hmin && dp <= hmax) {
      dp = glm::dot(glm::normalize(r.origin - V), D);
      if (dp * dp >= cosAngleSqr) {
        is_inside = true;
      }
    }
  }

  Cone3 cone{};
  cone.V = V;
  cone.D = D;
  cone.cosAngleSqr = cosAngleSqr;
  cone.hmin = hmin;
  cone.hmax = hmax;
  cone.isFinite = true;

#ifdef LSS_UNITTEST
  printf("cone V=(%g,%g,%g)\n", V.x, V.y, V.z);
  printf("     D=(%g,%g,%g)\n", D.x, D.y, D.z);
  printf("     cosAngleSqr=%g\n", cosAngleSqr);
  printf("     hmin=%g\n", hmin);
  printf("     hmax=%g\n", hmax);
#endif

  HitRecord rec_spheres{};
  rec_spheres.t = std::numeric_limits<float>::max();
  bool ret = HitSpheres(r, t_min, t_max, rec_spheres, is_inside);
  rec = rec_spheres;
  //bool ret = false;

  float hitT[2]{};
  glm::vec3 hitP[2]{};
  glm::vec3 hitN[2]{};
  char range_ends[2]{};
  ConeIntersectionType intersectionType = ConeDoQuery(r.origin, r.direction, cone, hitT, range_ends);
  ConeComputePoints(intersectionType, r.origin, r.direction, cone, hitT, hitP, hitN, range_ends, is_inside);

  HitRecord rec_cone{};
  rec_cone.t = std::numeric_limits<float>::max();
  switch (intersectionType) {
  case ConeIntersectionType::NONE:
    break;
  case ConeIntersectionType::POINT:
    break;
  case ConeIntersectionType::SEGMENT: {
    uint32_t idx{};
    idx = (hitT[0] > hitT[1]) ? 1 : 0;
    if (is_inside) {
      idx = (hitT[1] > hitT[0]) ? 1 : 0;
      if (std::isnan(hitP[idx].x)) { idx = 1 - idx; }
    }
    if (hitT[idx] > 0 && hitT[idx] >= t_min && hitT[idx] <= t_max && !std::isnan(hitP[idx].x)) {
      rec_cone.t = hitT[idx];
      rec_cone.p = r.at(rec_cone.t);
      rec_cone.SetFaceNormal(r, hitN[idx]);
      ret = true;
      break;
    }
    else {
      break;
    }
  }
  case ConeIntersectionType::RAY_POSITIVE: {
    if (hitT[0] > 0 && hitT[0] >= t_min && hitT[0] <= t_max && !std::isnan(hitP[0].x)) {
      rec_cone.t = hitT[0];
      rec_cone.p = r.at(rec_cone.t);
      rec_cone.SetFaceNormal(r, hitN[0]);
      ret = true;
      break;
    }
    else {
      break;
    }
  }
  case ConeIntersectionType::RAY_NEGATIVE:
    break;
  }


  if (rec_cone.t != std::numeric_limits<float>::max() && rec_spheres.t == std::numeric_limits<float>::max()) {
    rec = rec_cone;
  }
  else if (rec_cone.t == std::numeric_limits<float>::max() && rec_spheres.t != std::numeric_limits<float>::max()) {
    rec = rec_spheres;
  }
  else if (rec_cone.t != std::numeric_limits<float>::max() && rec_spheres.t != std::numeric_limits<float>::max()) {
    if (is_inside) {
      rec = rec_cone;
    }
    else {
      rec = (rec_cone.t < rec_spheres.t) ? rec_cone : rec_spheres;
    }
  }
  return ret;
}

bool LinearSweptSphere::do_hit_new(const Ray& r, float t_min, float t_max,
  HitRecord& rec) const {
  glm::vec3 ro = r.origin, rd = r.direction;
  glm::vec3 c0c1n = glm::normalize(c1 - c0);
  glm::vec3 pa = c0, pb = c1;
  float ra = r0, rb = r1;

  glm::vec3 ba = pb - pa;
  glm::vec3 oa = ro - pa;
  glm::vec3 ob = ro - pb;
  float rr = ra - rb;
  float m0 = glm::dot(ba, ba);
  float m1 = glm::dot(ba, oa);
  float m2 = glm::dot(ba, rd);
  float m3 = glm::dot(rd, oa);
  float m5 = glm::dot(oa, oa);
  float m6 = glm::dot(ob, rd);
  float m7 = glm::dot(ob, ob);

  const float EPS = 0;

  // ---------- inside test ----------
  float l = glm::clamp(m1 / m0, 0.0f, 1.0f);
  glm::vec3  pc = c0 + ba * l;
  float rcp = glm::mix(ra, rb, l);
  const float THRESH = 1e-4;

  float ba2 = glm::dot(ba, ba);
  float k = ba2 - rr * rr;

  //bool  inside = glm::dot(ro-pc,ro-pc) < rcp*rcp + THRESH;
  bool  inside = glm::dot(ro - pc, ro - pc) / ba2 * k < rcp * rcp + THRESH;

  if (inside) { rec.flag |= 4; }

#ifdef LSS_UNITTEST
  printf("inside=%d, %g < %g + %g\n", inside, glm::dot(ro - pc, ro - pc), rcp * rcp, THRESH);
#endif

  // body
  float d2 = m0 - rr * rr;
  float k2 = d2 - m2 * m2;
  float k1 = d2 * m3 - m1 * m2 + m2 * rr * ra;
  float k0 = d2 * m5 - m1 * m1 + m1 * rr * ra * 2.0 - m0 * ra * ra;
  float h = k1 * k1 - k0 * k2;
  if (h < 0.0) return false;

  float srt = sqrt(h);
  assert(k2 != 0);
  float t0 = (-srt - k1) / k2;
  float t1 = (srt - k1) / k2;
  float t = inside ? t1 : t0;

  if (t < 0.0) {
    t = inside ? t0 : t1;
    if (t < 0.0) return false;
  }

  // if( t<0.0 ) return vec4(-1.0);
  float y = m1 - ra * rr + t * m2;

  float t_cand = std::numeric_limits<float>::max();

  if (y > 0.0 && y < d2 && t > t_min && t <= t_max) {
    t_cand = t;
    rec.t = t;
    rec.p = r.at(rec.t);
    glm::vec3 n = glm::normalize(d2 * (oa + t * rd) - ba * y);
    rec.SetFaceNormal(r, n);
    rec.flag |= 0x1;
    return true;
  }

  // caps
  float h1 = m3 * m3 - m5 + ra * ra;
  float h2 = m6 * m6 - m7 + rb * rb;
  if (h1 <= 0.0 && h2 <= 0.0) return false;

  bool ret = false;

  if (h1 > 0.0)
  {
    float cands[] = {
      -m3 - sqrt(h1),
      -m3 + sqrt(h1)
    };

    for (uint32_t i = (inside ? 1 : 0); i < 2; i++) {
      t = cands[i];
      bool ok = (t < t_cand);
      if (ok && t >= t_min && t <= t_max) {
        t_cand = t;
        rec.t = t;
        rec.p = r.at(rec.t);
        glm::vec3 n = glm::normalize((oa + t * rd) / ra);
        rec.SetFaceNormal(r, n);
        ret = true;
        rec.flag |= 2;
      }
    }
  }
  if (h2 > 0.0)
  {
    float cands[] = {
      -m6 - sqrt(h2),
      -m6 + sqrt(h2)
    };

    for (uint32_t i = (inside ? 1 : 0); i < 2; i++) {
      t = cands[i];
      bool ok = (t < t_cand);
      if (ok && t >= t_min && t <= t_max) {
        t_cand = t;
        rec.t = t;
        rec.p = r.at(rec.t);
        glm::vec3 n = glm::normalize((ob + t * rd) / rb);
        rec.SetFaceNormal(r, n);
        ret = true;
        rec.flag |= 3;
      }
    }
  }
  return ret;
}

bool LinearSweptSphere::Hit(const Ray& r, float t_min, float t_max,
  HitRecord& rec) const {
  if (g_use_do_hit_old) {
    return do_hit_old(r, t_min, t_max, rec);
  }
  else {
    return do_hit_new(r, t_min, t_max, rec);
  }
}