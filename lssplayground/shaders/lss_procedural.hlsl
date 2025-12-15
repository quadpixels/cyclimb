RWTexture2D<float4> RenderTarget : register(u0);
RaytracingAccelerationStructure Scene : register(t0, space0);
StructuredBuffer<float3> LssPositions : register(t1);
StructuredBuffer<float> LssRadii : register(t2);

struct MyPayload
{
    float t;
};

struct MyAttributes
{
    float2 bary;
};

cbuffer PerSceneCb : register(b0)
{
    float4x4 inverse_view;
    float4x4 inverse_proj;
    int cam_mode;  // 0=perspective, 1=orthogonal
}

// My boilerplates
float3 TransformPosition(float4x4 m, float3 x)
{
    float4 x4 = float4(x, 0.0f);
    x4 = mul(m, x4);
    x4.x += m[0][3]; // [Col] [Row]
    x4.y += m[1][3];
    x4.z += m[2][3];
    return x4.xyz;
}

float3 TransformDirection(float4x4 m, float3 x)
{
    return (mul(m, float4(x, 0.0f))).xyz;
}

[shader("raygeneration")]
void RayGen()
{
    uint3 dri = DispatchRaysIndex();
    uint3 drd = DispatchRaysDimensions();
    RenderTarget[dri.xy] = float4(dri.xy * 1.0f / drd.xy, 0, 1);
    
    MyPayload payload;
    payload.t = -1;
    
    RayDesc ray;
    float2 d = (((DispatchRaysIndex().xy + 0.5f) / DispatchRaysDimensions().xy) * 2.f - 1.f);
    if (cam_mode == 0)
    {
        float3 target = TransformPosition(inverse_proj, float3(d.x, d.y, 1));
        float3 dir = TransformDirection(inverse_view, normalize(target));
        ray.Direction = dir;
        ray.Origin = TransformPosition(inverse_view, float3(0, 0, 0));
    }
    ray.TMin = 0.0;
    ray.TMax = 1e20;
    TraceRay(Scene,
        RAY_FLAG_NONE,
        0xFF,
        0,
        0,
        0,
        ray,
        payload
    );
}

[shader("miss")]
void Miss(inout MyPayload payload)
{
  //
}

[shader("closesthit")]
void ClosestHit(inout MyPayload payload, in MyAttributes attr)
{
    payload.t = RayTCurrent();
    uint pidx = PrimitiveIndex();
    float3 c0 = LssPositions[pidx * 2];
    float3 c1 = LssPositions[pidx * 2 + 1];
    float r0 = LssRadii[pidx * 2];
    float r1 = LssRadii[pidx * 2 + 1];
    
    uint3 dri = DispatchRaysIndex();
    //RenderTarget[dri.xy] = float4(r0 * 0.3f, r1 * 0.3f, 0, 1);
    RenderTarget[dri.xy] = float4(1, 1, 0, 1);
}

bool IntersectLSS(
    in float3 pa, in float ra,
    in float3 pb, in float rb,
    in float3 ro, in float3 rd, in float tmin, in float tmax, out float thit)
{
    float3 ba = pb - pa;
    float3 oa = ro - pa;
    float3 ob = ro - pb;
    float rr = ra - rb;
    float m0 = dot(ba, ba);
    float m1 = dot(ba, oa);
    float m2 = dot(ba, rd);
    float m3 = dot(rd, oa);
    float m5 = dot(oa, oa);
    float m6 = dot(ob, rd);
    float m7 = dot(ob, ob);

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
    
        for (uint i = (inside ? 1 : 0); i < 2; i++)
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

        for (uint i = (inside ? 1 : 0); i < 2; i++)
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

[shader("intersection")]
void Intersection()
{
    MyAttributes attr;
    uint pidx = PrimitiveIndex();
    float3 c0 = LssPositions[pidx * 2];
    float3 c1 = LssPositions[pidx * 2 + 1];
    float r0 = LssRadii[pidx * 2];
    float r1 = LssRadii[pidx * 2 + 1];
    
    float3 ro = ObjectRayOrigin();
    float3 rd = ObjectRayDirection();
    float tmax = RayTCurrent();
    float tmin = RayTMin();
    
    float thit;
    
    bool hit = IntersectLSS(c0, r0, c1, r1, ro, rd, tmin, tmax, thit);
    if (hit)
    {
        ReportHit(hit, 0, attr);
    }
}
