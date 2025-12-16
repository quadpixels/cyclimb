#include <glm/glm.hpp>

#include <stdint.h>
#include <vector>
#include <thread>

#include <d3d12.h>

template<typename Vec3, typename Float>
static std::vector<uint8_t> g_bytes;

static uint32_t             W, H;

// The snapshot of LSS poses and radii when the render starts.
template<typename Vec3, typename Float>
std::vector<glm::vec3> lss_poses;

template<typename Vec3, typename Float>
std::vector<float> lss_radii;

template<typename Vec3, typename Float>
glm::mat4 inv_view;

template<typename Vec3, typename Float>
glm::mat4 inv_proj;

template<typename Vec3, typename Float>
bool IntersectLSS(
  const glm::vec3& _pa, float _ra,
  const glm::vec3& _pb, float _rb,
  const glm::vec3& _ro, const glm::vec3& _rd, float tmin, float tmax, float& thit) {
  thit = -1;

  Vec3 pa = _pa, pb = _pb, ro = _ro, rd = _rd;
  Float ra = _ra, rb = _rb;

  Vec3 ba = pb - pa;
  Vec3 oa = ro - pa;
  Vec3 ob = ro - pb;
  Float rr = ra - rb;
  Float m0 = glm::dot(ba, ba);
  Float m1 = glm::dot(ba, oa);
  Float m2 = glm::dot(ba, rd);
  Float m3 = glm::dot(rd, oa);
  Float m5 = glm::dot(oa, oa);
  Float m6 = glm::dot(ob, rd);
  Float m7 = glm::dot(ob, ob);
  bool inside = false;

  // BODY
  Float d2 = m0 - rr * rr;
  Float k2 = d2 - m2 * m2;
  Float k1 = d2 * m3 - m1 * m2 + m2 * rr * ra;
  Float k0 = d2 * m5 - m1 * m1 + m1 * rr * ra * 2.0 - m0 * ra * ra;
  Float h = k1 * k1 - k0 * k2;
  if (h < 0.0)
    return false;

  Float srt = sqrt(h);
  Float t0 = (-srt - k1) / k2;
  Float t1 = (srt - k1) / k2;
  Float t = inside ? t1 : t0;

  if (t < 0.0)
  {
    t = inside ? t0 : t1;
    if (t < 0.0)
      return false;
  }

  Float y = m1 - ra * rr + t * m2;

  Float t_cand = 1e20;

  if (y > 0.0 && y < d2 && t > tmin && t <= tmax)
  {
    t_cand = t;
    thit = t;
    // float3 n = normalize(d2 * (oa + t * rd) - ba * y);
    return true;
  }

  // caps
  Float h1 = m3 * m3 - m5 + ra * ra;
  Float h2 = m6 * m6 - m7 + rb * rb;
  if (h1 <= 0.0 && h2 <= 0.0)
    return false;

  bool ret = false;

  if (h1 > 0.0)
  {
    Float cands[] =
    {
        -m3 - sqrt(h1),
        -m3 + sqrt(h1)
    };

    for (uint32_t i = (inside ? 1 : 0); i < 2; i++)
    {
      t = cands[i];
      bool ok = (t < t_cand);
      if (ok && t >= tmin && t <= tmax)
      {
        t_cand = t;
        thit = t;
        //rec.p = r.at(rec.t);
        //glm::vec3 n = glm::normalize((oa + t * rd) / ra);
        ret = true;
      }
    }
  }
  if (h2 > 0.0)
  {
    Float cands[] =
    {
        -m6 - sqrt(h2),
        -m6 + sqrt(h2)
    };

    for (uint32_t i = (inside ? 1 : 0); i < 2; i++)
    {
      t = cands[i];
      bool ok = (t < t_cand);
      if (ok && t >= tmin && t <= tmax)
      {
        t_cand = t;
        thit = t;
        //rec.p = r.at(rec.t);
        //glm::vec3 n = glm::normalize((ob + t * rd) / rb);
        //rec.SetFaceNormal(r, n);
        //rec.mat_ptr = this - > mat_ptr;
        ret = true;
      }
    }
  }
  return ret;
}

glm::vec3 TransformPosition(const glm::mat4& m, const glm::vec3& x)
{
  glm::vec4 x4 = glm::vec4(x, 1.0f);
  x4 = m * x4;
  return glm::vec3(x4);
}

glm::vec3 TransformDirection(const glm::mat4& m, const glm::vec3& x)
{
  glm::vec4 x4 = glm::vec4(x, 0.0f);
  x4 = m * x4;
  return glm::vec3(x4);
}

template<typename Vec3, typename Float>  // CXX 14 feature
static std::thread* cpu_runner{ nullptr };

template<typename Vec3, typename Float>
static std::vector<std::thread*> g_threads;

template<typename Vec3, typename Float>
bool IsCPUDone() {
  return (g_threads<Vec3, Float>.empty() && cpu_runner<Vec3, Float> == nullptr);
}

template bool IsCPUDone<glm::dvec3, double>();
template bool IsCPUDone<glm::vec3, float>();

template<typename Vec3, typename Float>
void InitCPURender(uint32_t w, uint32_t h,
  std::vector<glm::vec3> ps,
  std::vector<float> rs,
  glm::mat4 iv, glm::mat4 ip
) {
  if (!cpu_runner<Vec3, Float>) {
    cpu_runner<Vec3, Float> = new std::thread([=]() {

      W = w; H = h;
      g_bytes<Vec3, Float>.resize(4ULL * W * H);
      lss_poses<Vec3, Float> = ps; lss_radii<Vec3, Float> = rs;
      inv_proj<Vec3, Float> = ip; inv_view<Vec3, Float> = iv;

      std::thread* thd = new std::thread([=]() {
        for (uint32_t y = 0; y < H; y++) {
          for (uint32_t x = 0; x < W; x++) {
            float     u = x * 1.0f / (W - 1);
            float     v = y * 1.0f / (H - 1);
            glm::vec3 ro = TransformPosition(inv_view<Vec3, Float>, glm::vec3(0, 0, 0));
            glm::vec2 d(u * 2.0f - 1.0f, v * 2.0f - 1.0f);
            //d.y *= -1;
            glm::vec3 target = TransformPosition(inv_proj<Vec3, Float>, glm::vec3(d.x, -d.y, 1.0f));
            glm::vec3 rd = TransformDirection(inv_view<Vec3, Float>, glm::normalize(target));
            float thit;
            uint8_t* ptr = &(g_bytes<Vec3, Float>[4 * (y * W + x)]);

            if (IntersectLSS<Vec3, Float>(
              lss_poses<Vec3, Float>[0], lss_radii<Vec3, Float>[0],
              lss_poses<Vec3, Float>[1], lss_radii<Vec3, Float>[1],
              ro, rd,
              0, 1e20, thit
            )) {
              ptr[0] = 255;
              ptr[1] = 255;
              ptr[2] = 25;
              ptr[3] = 255;
            }
            else {
              ptr[0] = 25;
              ptr[1] = 25;
              ptr[2] = 25;
              ptr[3] = 255;
            }
          }
        }
        });
      g_threads<Vec3, Float>.push_back(thd);

      if (!g_threads<Vec3, Float>.empty()) {
        for (uint32_t i = 0; i < g_threads<Vec3, Float>.size(); i++) {
          g_threads<Vec3, Float>[i]->join();
        }
        g_threads<Vec3, Float>.clear();
      }
    });
    cpu_runner<Vec3, Float> = nullptr;
  }
}

template
void InitCPURender<glm::dvec3, double>(uint32_t w, uint32_t h,
  std::vector<glm::vec3> ps,
  std::vector<float> rs,
  glm::mat4 iv, glm::mat4 ip
);

template
void InitCPURender<glm::vec3, float>(uint32_t w, uint32_t h,
  std::vector<glm::vec3> ps,
  std::vector<float> rs,
  glm::mat4 iv, glm::mat4 ip
);

template<typename Vec3, typename Float>
void UpdateCPURenderResults(ID3D12Resource* res)
{
  uint8_t* mapped{};
  res->Map(0, nullptr, (void**)&mapped);
  memcpy(mapped, g_bytes<Vec3, Float>.data(), g_bytes<Vec3, Float>.size());
  res->Unmap(0, nullptr);
}


template void UpdateCPURenderResults<glm::dvec3, double>(ID3D12Resource* res);
template void UpdateCPURenderResults<glm::vec3, float>(ID3D12Resource* res);