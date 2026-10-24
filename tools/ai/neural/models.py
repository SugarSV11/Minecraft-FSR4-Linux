"""Candidate architectures for the neural corrector of the FG x3/x4 generated frames (docs/NEURAL_ARCHITECTURE.md). All take the 22-channel sample of dataset.py (floats in [0,1]) and
return the corrected RGB. Everything is 3x3 convolutions + ReLU/bilinear resampling so that it maps directly onto Vulkan compute.
  ResCNN     A: compact residual CNN                  out = G + f(x)
  TinyUNet   B: 2-level U-Net (larger receptive field for disocclusion holes)   out = G + f(x)
  Selector   C/D: mask- and vector-guided mixer: a small net looks at all features and predicts per-pixel softmax weights over the six candidates {G, A, B, C0, C1, blend} plus a small residual
The 'flag' (cheap detector) is used by the losses and by selective inference, never as a hard dependency of the network."""
import torch, torch.nn as nn, torch.nn.functional as F
NC = 22
def split(x):
    G, A, B, C0, C1 = x[:, 0:3], x[:, 3:6], x[:, 6:9], x[:, 9:12], x[:, 12:15]; Fh = x[:, 15:18]; M = x[:, 18:21]; T = x[:, 21:22]
    return G, A, B, C0, C1, Fh, M, T
def flag_mask(x, mv_thr=1.5 / 64.0, ab_thr=12 / 255.0):
    """Cheap per-pixel detector of 'the generator may be wrong here': hole-filled, depth-inconsistent (no source / inconsistent), large motion, or F0/F1 samples disagree."""
    G, A, B, C0, C1, Fh, M, T = split(x)
    nosrc = Fh[:, 0:1] < 0.5; incons = (Fh[:, 0:1] > 0.5) & (Fh[:, 1:2] < 0.5)
    mv = ((M[:, 0:2] - 0.5).abs().amax(1, keepdim=True) > mv_thr)
    ab = ((A - B).abs().amax(1, keepdim=True) > ab_thr)
    return (nosrc | incons | (mv & ab) | ab).float()
def tile_flag(fl, tile=16, dil=1):
    t = F.max_pool2d(fl, tile, tile); t = F.max_pool2d(t, 2 * dil + 1, 1, dil); return t
def conv(i, o, k=3): return nn.Conv2d(i, o, k, padding=k // 2)
class ResCNN(nn.Module):
    def __init__(s, c=16, depth=4):
        super().__init__(); s.inp = conv(NC, c); s.mid = nn.ModuleList([conv(c, c) for _ in range(depth - 2)]); s.out = conv(c, 3); nn.init.zeros_(s.out.weight); nn.init.zeros_(s.out.bias)
    def forward(s, x):
        h = F.relu(s.inp(x))
        for m in s.mid: h = F.relu(m(h))
        return x[:, :3] + s.out(h)
class TinyUNet(nn.Module):
    def __init__(s, c=12):
        super().__init__(); s.e0 = conv(NC, c); s.e1 = conv(c, 2 * c); s.e2 = conv(2 * c, 4 * c); s.d1 = conv(4 * c + 2 * c, 2 * c); s.d0 = conv(2 * c + c, c); s.out = conv(c, 3)
        nn.init.zeros_(s.out.weight); nn.init.zeros_(s.out.bias)
    def forward(s, x):
        e0 = F.relu(s.e0(x)); e1 = F.relu(s.e1(F.avg_pool2d(e0, 2))); e2 = F.relu(s.e2(F.avg_pool2d(e1, 2)))
        d1 = F.relu(s.d1(torch.cat([F.interpolate(e2, scale_factor=2, mode="bilinear", align_corners=False), e1], 1)))
        d0 = F.relu(s.d0(torch.cat([F.interpolate(d1, scale_factor=2, mode="bilinear", align_corners=False), e0], 1)))
        return x[:, :3] + s.out(d0)
class Selector(nn.Module):
    def __init__(s, c=12, unet=True, res_scale=0.25):
        super().__init__(); s.body = TinyUNet(c) if unet else ResCNN(c); s.body.out = conv(c, 6 + 3); nn.init.zeros_(s.body.out.weight); nn.init.zeros_(s.body.out.bias); s.rs = res_scale; s.unet = unet
        with torch.no_grad(): s.body.out.bias[0] = 4.0   # start as the identity (select G)
    def forward_w(s, x):
        G, A, B, C0, C1, Fh, M, T = split(x); blend = (1 - T) * C0 + T * C1
        h = s._feat(x); logits, res = h[:, :6], h[:, 6:9]
        w = torch.softmax(logits, 1); cand = torch.stack([G, A, B, C0, C1, blend], 1)
        return (w.unsqueeze(2) * cand).sum(1) + s.rs * torch.tanh(res), w
    def forward(s, x): return s.forward_w(x)[0]
    def _feat(s, x):
        b = s.body
        if s.unet:
            e0 = F.relu(b.e0(x)); e1 = F.relu(b.e1(F.avg_pool2d(e0, 2))); e2 = F.relu(b.e2(F.avg_pool2d(e1, 2)))
            d1 = F.relu(b.d1(torch.cat([F.interpolate(e2, scale_factor=2, mode="bilinear", align_corners=False), e1], 1)))
            d0 = F.relu(b.d0(torch.cat([F.interpolate(d1, scale_factor=2, mode="bilinear", align_corners=False), e0], 1))); return b.out(d0)
        h = F.relu(b.inp(x))
        for m in b.mid: h = F.relu(m(h))
        return b.out(h)
def rule_base(x):
    """R: the generator's frame with its hole-filled pixels replaced by the temporal blend (traditional baseline, docs/NEURAL_EVALUATION.md session 12)."""
    G, A, B, C0, C1, Fh, M, T = split(x); bl = (1 - T) * C0 + T * C1; m = (Fh[:, 0:1] < 0.5).float(); return m * bl + (1 - m) * G
class Sel2(nn.Module):
    """Session-12 selection family: softmax over K=7 candidates {R, G, A, B, C0, C1, blend}, NO residual head (what the Vulkan kernel can run). Starts as the rule R.
    kind: 'cnn' = conv22->c ->c ->c ->K (as the deployed kernel); 'ctx' = + a 1/4-resolution context branch (spatial context, receptive field +24 px); 'unet' = 2-level U-Net body."""
    K = 7
    def __init__(s, c=8, kind="cnn", gate=False, nb=False):
        super().__init__(); s.kind = kind; s.c = c; s.gate = gate; s.prior = 0.0; s.nb = nb; s.K = 5 if nb else 7   # nb: no-blend family {G, A, B, C0, C1}: only REAL pixels, nothing that averages two images
        if kind == "unet": s.body = TinyUNet(c); s.body.out = conv(c, s.K)
        else:
            s.body = ResCNN(c, 4); s.body.out = conv(c, s.K)
            if kind == "ctx": s.body.inp = conv(NC + c, c); s.c1 = conv(NC, c); s.c2 = conv(c, c)
        nn.init.zeros_(s.body.out.weight); nn.init.zeros_(s.body.out.bias)
        with torch.no_grad(): s.body.out.bias[0] = 3.0
    def cands(s, x):
        G, A, B, C0, C1, Fh, M, T = split(x)
        if s.nb: return torch.stack([G, A, B, C0, C1], 1)
        return torch.stack([rule_base(x), G, A, B, C0, C1, (1 - T) * C0 + T * C1], 1)
    def forward_w(s, x):
        b = s.body
        if s.kind == "unet":
            e0 = F.relu(b.e0(x)); e1 = F.relu(b.e1(F.avg_pool2d(e0, 2))); e2 = F.relu(b.e2(F.avg_pool2d(e1, 2)))
            d1 = F.relu(b.d1(torch.cat([F.interpolate(e2, scale_factor=2, mode="bilinear", align_corners=False), e1], 1)))
            d0 = F.relu(b.d0(torch.cat([F.interpolate(d1, scale_factor=2, mode="bilinear", align_corners=False), e0], 1))); lg = b.out(d0)
        else:
            if s.kind == "ctx":
                q = F.avg_pool2d(x, 4, ceil_mode=True); q = F.relu(s.c1(q)); q = F.relu(s.c2(q)); q = F.interpolate(q, size=x.shape[2:], mode="bilinear", align_corners=False); h = F.relu(b.inp(torch.cat([x, q], 1)))
            else: h = F.relu(b.inp(x))
            for m in b.mid: h = F.relu(m(h))
            lg = b.out(h)
        if s.prior: lg = lg + s.prior * torch.eye(s.K, device=lg.device)[0].view(1, -1, 1, 1)   # inference-time conservatism: extra logit for the rule candidate (chosen on VALIDATION)
        w = torch.softmax(lg, 1); out = (w.unsqueeze(2) * s.cands(x)).sum(1)
        if s.gate: m = F.max_pool2d(flag_mask(x), 17, 1, 8); out = m * out + (1 - m) * (x[:, :3] if s.nb else rule_base(x))   # outside the (dilated) suspicious area the output IS the rule: no learned noise on ordinary pixels
        return out, w
    def forward(s, x): return s.forward_w(x)[0]
class ResB(nn.Module):
    """Residual corrector on top of the rule: out = R + f(x) (architecture B of the session-12 comparison)."""
    def __init__(s, c=16, depth=4): super().__init__(); s.r = ResCNN(c, depth)
    def forward(s, x): return rule_base(x) + (s.r(x) - x[:, :3])

def make(name):
    if name == "res16": return ResCNN(16, 4)
    if name == "res8": return ResCNN(8, 3)
    if name == "unet12": return TinyUNet(12)
    if name == "unet8": return TinyUNet(8)
    if name == "sel12": return Selector(12, True)
    if name == "sel8": return Selector(8, True)
    if name == "selres16": return Selector(16, False)
    if name == "selres8": return Selector(8, False)
    if name == "selres12": return Selector(12, False)
    if name.startswith("sb"):   # sb8 / sb16 (cnn), sbctx8 (spatial context), sbunet12
        k = "unet" if "unet" in name else "ctx" if "ctx" in name else "cnn"; return Sel2(int("".join(ch for ch in name if ch.isdigit())), k, name.startswith(("sbg", "sbng")), name.startswith("sbn"))
    if name == "resb16": return ResB(16, 4)
    raise ValueError(name)
def macs_1080p(model):
    """Multiply-accumulates per 1080p frame if run on every pixel (counted from the convolution layers; U-Net levels run at 1/4 and 1/16 of the pixels)."""
    px = 1920 * 1080; tot = 0
    for n, m in model.named_modules():
        if isinstance(m, nn.Conv2d):
            scale = 1.0
            if isinstance(model, (TinyUNet,)) or (isinstance(model, Selector) and model.unet): scale = {"e0": 1, "e1": 0.25, "e2": 1 / 16, "d1": 0.25, "d0": 1, "out": 1}.get(n.split(".")[-1], 1)
            tot += m.in_channels * m.out_channels * 9 * px * scale
    return tot
