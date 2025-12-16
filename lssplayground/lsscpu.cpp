#include <glm/glm.hpp>

#include <stdint.h>
#include <vector>
#include <thread>

#include <d3d12.h>

static std::vector<uint8_t> g_bytes;
static uint32_t             W, H;
static std::vector<std::thread*> g_threads;

// The snapshot of LSS poses and radii when the render starts.
std::vector<glm::vec3> lss_poses;
std::vector<float> lss_radii;

glm::mat4 inv_view, inv_proj;

bool IntersectLSS(
  const glm::vec3& pa, float ra,
  const glm::vec3& pb, float rb,
  const glm::vec3& ro, const glm::vec3& rd, float tmin, float tmax, float& thit) {
  thit = -1;

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
  bool inside = false;

  // BODY
  float d2 = m0 - rr * rr;
  float k2 = d2 - m2 * m2;
  float k1 = d2 * m3 - m1 * m2 + m2 * rr * ra;
  float k0 = d2 * m5 - m1 * m1 + m1 * rr * ra * 2.0 - m0 * ra * ra;
  float h = k1 * k1 - k0 * k2;
  if (h < 0.0)
    return false;

  float srt = sqrt(h);
  float t0 = (-srt - k1) / k2;
  float t1 = (srt - k1) / k2;
  float t = inside ? t1 : t0;

  if (t < 0.0)
  {
    t = inside ? t0 : t1;
    if (t < 0.0)
      return false;
  }

  float y = m1 - ra * rr + t * m2;

  float t_cand = 1e20;

  if (y > 0.0 && y < d2 && t > tmin && t <= tmax)
  {
    t_cand = t;
    thit = t;
    // float3 n = normalize(d2 * (oa + t * rd) - ba * y);
    return true;
  }

  // caps
  float h1 = m3 * m3 - m5 + ra * ra;
  float h2 = m6 * m6 - m7 + rb * rb;
  if (h1 <= 0.0 && h2 <= 0.0)
    return false;

  bool ret = false;

  if (h1 > 0.0)
  {
    float cands[] =
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
    float cands[] =
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
 
static std::thread* cpu_runner{ nullptr };

bool IsCPUDone() {
  return (g_threads.empty() && cpu_runner == nullptr);
}

void InitCPURender(uint32_t w, uint32_t h,
  std::vector<glm::vec3> ps,
  std::vector<float> rs,
  glm::mat4 iv, glm::mat4 ip
) {
  if (!cpu_runner) {
    cpu_runner = new std::thread([=]() {

      W = w; H = h;
      g_bytes.resize(4ULL * W * H);
      lss_poses = ps; lss_radii = rs;
      inv_proj = ip; inv_view = iv;

      std::thread* thd = new std::thread([=]() {
        for (uint32_t y = 0; y < H; y++) {
          for (uint32_t x = 0; x < W; x++) {

            float     u = x * 1.0f / (W - 1);
            float     v = y * 1.0f / (H - 1);
            glm::vec3 ro = TransformPosition(inv_view, glm::vec3(0, 0, 0));
            glm::vec2 d(u * 2.0f - 1.0f, v * 2.0f - 1.0f);
            //d.y *= -1;
            glm::vec3 target = TransformPosition(inv_proj, glm::vec3(d.x, -d.y, 1.0f));
            glm::vec3 rd = TransformDirection(inv_view, glm::normalize(target));
            float thit;
            uint8_t* ptr = &(g_bytes[4 * (y * W + x)]);

            if (IntersectLSS(
              lss_poses[0], lss_radii[0],
              lss_poses[1], lss_radii[1],
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
      g_threads.push_back(thd);

      if (!g_threads.empty()) {
        for (uint32_t i = 0; i < g_threads.size(); i++) {
          g_threads[i]->join();
        }
        g_threads.clear();
      }
    });
    cpu_runner = nullptr;
  }
}

void UpdateCPURenderResults(ID3D12Resource* res)
{
  uint8_t* mapped{};
  res->Map(0, nullptr, (void**)&mapped);
  memcpy(mapped, g_bytes.data(), g_bytes.size());
  res->Unmap(0, nullptr);
}
