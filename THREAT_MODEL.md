<!--
#
# Licensed to the Apache Software Foundation (ASF) under one
# or more contributor license agreements.  See the NOTICE file
# distributed with this work for additional information
# regarding copyright ownership.  The ASF licenses this file
# to you under the Apache License, Version 2.0 (the
# "License"); you may not use this file except in compliance
# with the License.  You may obtain a copy of the License at
#
# http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing,
# software distributed under the License is distributed on an
# "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
#  KIND, either express or implied.  See the License for the
# specific language governing permissions and limitations
# under the License.
#
-->

# Apache NimBLE (mynewt-nimble) Security Threat Model (draft)

> **Status: v0 draft for PMC review.** Drafted by the ASF Security team
> from public artefacts (the in-repo source layout under `nimble/host`,
> `nimble/controller`, `nimble/host/services`, and `syscfg.yml`) against
> the Scovetta rubric, plus general Bluetooth-LE domain knowledge. **No
> maintainer has reviewed it yet.** Provenance tags: *(documented)* /
> *(maintainer — none yet)* / *(inferred)*, with every load-bearing
> *(inferred)* claim routed to a numbered §14 question. This is a
> starting point to react to, not a finished model.

## §1 Header

- **Project:** Apache NimBLE — a full Bluetooth Low Energy (BLE) host +
  controller stack.
- **Repository / commit:** `apache/mynewt-nimble`, `master` @ `793cb1612088` (2026-06-09).
- **Drafted:** 2026-06-13, ASF Security team (v0 draft from public artefacts).
- **Companion repos this round:** `apache/mynewt-core` (the RTOS NimBLE
  runs on, and which provides the management transport that may sit *on
  top of* a BLE L2CAP channel) and `apache/mynewt-mcumgr` (uses a BLE
  connection as one SMP transport — so NimBLE is the carrier, mcumgr the
  payload).
- **What triggers a revision:** a new Bluetooth spec feature
  (new PDU types, a new pairing method, a new LL control procedure);
  a change to the Security Manager's supported association models; a
  change to the HCI host/controller split; or a new GATT service shipped
  in-tree. Internal refactors that do not change a wire-facing parser or
  the SM cryptography do not.

## §2 Scope and intended use

NimBLE is **the BLE protocol stack a firmware author links into a
device**. It is not an application; it is the machinery that turns radio
PDUs into connection/GATT events and back. It can be built as a
**combined** host+controller, **host-only** (talking to an external
controller over HCI), or **controller-only** (exposing HCI to an external
host). *(documented: `nimble/{host,controller,transport}` layout; §14 Q1)*

### Component families (each at its own trust/surface tier)

| Family | Source | What it parses / does | Radio-trust tier |
| --- | --- | --- | --- |
| **Controller / Link Layer (LL)** | `nimble/controller/src/ble_ll_*` | Lowest level: advertising, scanning, connection establishment, LL control PDUs, channel selection, encryption (`ble_ll_crypto`), RPA resolution, whitelist, DTM. | **Tier 0 — raw radio.** Processes PDUs from *anyone in range* before any connection or authentication exists. |
| **HCI transport** | `nimble/transport`, `ble_hs_hci*`, `ble_ll_hci*` | The command/event boundary between host and controller. | Boundary **only when split**; in a combined build it is internal. |
| **Host — L2CAP** | `ble_l2cap*` (+ `_coc`, `_sig`) | Channel multiplexing, fragmentation/reassembly, connection-oriented channels, signalling. | **Tier 1 — connected peer**, pre- or post-encryption. |
| **Host — ATT / GATT** | `ble_att*`, `ble_gattc`, `ble_gatts` | The attribute protocol server/client and GATT layer above it. | Tier 1; the server parses every ATT request from the peer. |
| **Host — GAP + advertising data** | `ble_gap`, `ble_hs_adv` | Connection/advertisement state machine; parses advertising/scan-response data. | Tier 0/1. |
| **Security Manager (SM)** | `ble_sm`, `ble_sm_sc`, `ble_sm_lgcy`, `ble_sm_alg`, `ble_sm_cmd` | Pairing, key generation, MITM protection, LE Secure Connections (ECDH P-256) and legacy pairing. **This is where NimBLE's actual security *properties* live.** | Tier 1; pairing PDUs are attacker-reachable; the *output* is the encryption/authentication of the link. |
| **Privacy** | `ble_hs_pvcy`, `ble_ll_resolv` | Resolvable Private Address (RPA) generation/resolution. | Tier 0. |
| **In-tree GATT services** | `nimble/host/services/{ans,bas,bleuart,dis,gap,ias,ipss,lls,tps}` | Example/standard GATT services. | Application-tier; mostly demo/standard profiles (§3). |

### What NimBLE is *not*

- Not the application's GATT logic. NimBLE provides the protocol and the
  *mechanism* to require encryption/authentication on a characteristic;
  **which** characteristics require it is the application's choice (§10).
- Not the radio PHY or the RF hardware (`drivers/` are thin HW shims).
- Not key storage at rest — bond/key persistence uses the host OS's store
  (`apache/mynewt-core` or the porting layer). *(inferred — §14 Q2)*

## §3 Out of scope (explicit non-goals)

1. **Application GATT services and their access-control choices.** If an
   app exposes a sensitive characteristic with no encryption/auth
   requirement, that is the app's decision, not a NimBLE defect (§10).
   The in-tree `host/services/*` are standard/demo profiles in this same
   category.
2. **The Bluetooth specification itself.** Where the spec mandates an
   unauthenticated or weak-by-modern-standards behaviour (open
   advertising, passive-eavesdroppable pre-encryption traffic, "Just
   Works" pairing with no MITM protection, legacy pairing), NimBLE
   conforming to the spec is **not** a NimBLE vulnerability (§11a).
3. **The radio PHY / RF front-end / hardware** and the timing of the
   link — jamming, RF interference, and physical-layer attacks are out.
4. **The host RTOS, its scheduler, and key-at-rest storage** — modelled
   in `apache/mynewt-core` / the porting layer.
5. **mcumgr / newtmgr management payloads** that *ride on top of* a BLE
   L2CAP channel — NimBLE is the carrier; the SMP server is modelled in
   `apache/mynewt-mcumgr` and `apache/mynewt-core`'s `mgmt/`.
6. **Physical / invasive and side-channel attacks** on the device.
7. **Supply-chain / build hygiene.**

## §4 Trust boundaries and data flow

The trust boundary is **the antenna**. What differs from a wired stack is
that there are **graduated** radio-trust tiers, and the same code base
processes input at every tier:

```
 Tier 0  RAW RADIO (anyone in range, NO connection, NO auth)
   adv / scan-req / scan-rsp / connect-ind / LL-control PDUs
        |  ble_ll_* (controller)         ble_hs_adv (host, adv-data parse)
        v
 Tier 1  CONNECTED PEER (a link exists; may be UNENCRYPTED)
   L2CAP (+reassembly) -> ATT requests -> GATT ; SM pairing PDUs
        |  ble_l2cap* / ble_att_svr / ble_sm*
        v
 [ Security Manager pairing ] --(if an *authenticated* method used)-->
        v
 Tier 2  ENCRYPTED + (optionally) AUTHENTICATED LINK
   AES-CCM link encryption (ble_ll_crypto) ; bonded keys
        |
        v
 application GATT characteristics  (permissions are the APP's choice, §10)

 HCI boundary (only in split host/controller builds):
   host <== HCI events/ACL ==> controller   (trusted in combined builds)
```

Two consequences: (1) **most NimBLE code runs at Tier 0/1 against fully
untrusted input** — memory safety there is the core obligation; (2) the
*transition* from Tier 1 to Tier 2 **is** NimBLE's security product — the
correctness of the Security Manager's pairing crypto is what authenticates
and encrypts the link.

## §5 Assumptions about the environment

- **Embedded, single address space, no MMU/process isolation.** A
  memory-safety bug in any PDU parser is a whole-device compromise.
  *(inferred — §14 Q3)*
- **In a combined build, host and controller trust each other** over the
  internal HCI; in a split build, the HCI transport is itself a trust
  boundary and each side must tolerate a malformed/ hostile peer across
  it. *(inferred — §14 Q1)*
- **The RNG provided by the platform is cryptographically adequate** for
  pairing nonces / ECDH key generation / RPA. NimBLE consumes it; its
  quality is the platform's. *(inferred — §14 Q4)*
- **Bond/key storage provided by the host is confidential and
  integrity-protected.** *(inferred — §14 Q2)*

## §5a Build-time and configuration variants

- **Role build (combined / host-only / controller-only)** changes which
  parsers are present and whether HCI is a boundary. *(documented: layout)*
- **Supported pairing methods / SM features** are syscfg-selectable
  (LE Secure Connections vs legacy, bonding, MITM, OOB, key-distribution
  set). The *security posture of the link depends on which are enabled
  and which the application requests.* *(inferred — §14 Q5)*
- **Privacy (RPA)** on/off and the resolving-list size. *(documented:
  `ble_hs_pvcy`, `ble_ll_resolv`)*
- **Maximum MTU / number of connections / L2CAP COC buffers** bound
  reassembly and allocation. *(inferred — §14 Q6)*
- **Direct Test Mode (`ble_ll_dtm`)** is a certification/test feature;
  whether it is reachable in production builds matters. *(documented:
  `ble_ll_dtm`; §14 Q7)*

## §6 Assumptions about inputs

**Every radio PDU is attacker-controllable** to anyone within range. The
question for each surface is only *which tier* it is reachable at.

### Radio-input trust table

| Surface | Source code | Reachable by | Must enforce |
| --- | --- | --- | --- |
| Advertising / scan-response **data** | `ble_hs_adv`, `ble_ll_adv`, `ble_ll_scan` | anyone in range, no connection | memory-safe parse of arbitrary/truncated/over-length AD structures; no OOB on malformed length fields *(inferred — §14 Q8)* |
| `connect-ind` / connection setup | `ble_ll_conn`, `ble_gap` | anyone in range | bounded state allocation; safe handling of malformed connection parameters *(inferred — §14 Q8)* |
| **LL control PDUs** | `ble_ll_ctrl` | a connected peer (Tier 1, pre-encryption) | safe handling of every control opcode incl. length/feature/PHY-update, version, and **encryption-start** procedures *(inferred — §14 Q9)* |
| HCI commands/events | `ble_ll_hci*`, `ble_hs_hci*` | the HCI peer (split build) or internal | length/parameter validation on every event/command *(inferred — §14 Q1)* |
| **L2CAP** packets + reassembly | `ble_l2cap`, `_coc`, `_sig` | connected peer | reassembly bounds (no overflow on a lying L2CAP length); COC credit/flow safety *(inferred — §14 Q10)* |
| **ATT requests** | `ble_att_svr` | connected peer (pre/post-encryption per char permission) | bounds on every ATT opcode (READ/WRITE/PREPARE-WRITE queue, FIND, READ-BLOB offset); the prepared-write queue is a classic resource sink *(inferred — §14 Q11)* |
| **SM pairing PDUs** | `ble_sm*` | connected peer, pre-encryption | correct pairing state machine; **validate the peer's ECDH public key** (reject invalid-curve / zero / debug keys); no key/nonce reuse; constant-time where it matters *(inferred — §14 Q12, the highest-value crypto question)* |

### Size / shape / rate

- Reassembly and allocation are bounded by the configured MTU / buffer
  counts (§5a); a peer that lies about a length field must not breach
  those bounds. *(inferred — §14 Q6/Q10)*
- No rate guarantee against a flooding/jamming peer (§9). *(inferred)*

## §7 Adversary model

### Actors

| Actor | In scope? | Capabilities |
| --- | --- | --- |
| **Anyone in radio range** | **yes — primary** | passively sniff all pre-encryption traffic; inject/spoof advertising; initiate connections; send malformed PDUs at every layer; attempt/abort pairing; replay. |
| **Active MITM during pairing** | **yes** | sit between two pairing devices. NimBLE's defence is an *authenticated* SM association model (Passkey / Numeric Comparison / OOB); "Just Works" provides **none** by spec (§9, §11a). |
| **A bonded/paired peer turned malicious** | **conditionally** | has a valid LTK; can send well-formed encrypted requests. In-model only for memory safety / bounds in the request parsers — not for "a bonded peer can use the services it was granted." |
| **HCI peer** (split builds) | **yes** | a malicious external controller/host across the HCI transport. |
| **Application firmware author** | **out of scope** | trusted; chooses pairing requirements and characteristic permissions. |
| **Physical / RF-layer attacker** (jam, glitch, decap) | **out of scope** | §3.3, §3.6. |
| **Side-channel observer** of SM crypto | **partially — §14 Q13** | timing/DPA on pairing; NimBLE's constant-time posture is unconfirmed. |

### The graduated-trust asymmetry

Unlike a wired protocol, **most BLE traffic is unauthenticated by spec**
up to the moment pairing completes. The adversary's reach is therefore
huge at Tiers 0/1 and is *reduced* only by (a) NimBLE's parser robustness
and (b) the application requiring an authenticated pairing before
exposing anything sensitive. A finding that "an unpaired peer can read
characteristic X" is the *application's* permission choice (§10), not a
NimBLE bug — **unless** NimBLE failed to enforce a permission the app
*did* set, which is VALID.

## §8 Security properties the project provides

For each: condition, violation symptom, severity, provenance.

1. **Memory-safe parsing of malformed PDUs across LL / HCI / L2CAP /
   ATT / SM**, given a correct platform. *(inferred — §14 Q8–Q12; the
   foundational property)*
   - *Violation:* a crafted PDU (bad length, malformed AD, oversized ATT
     write, lying L2CAP reassembly length) → OOB read/write or overflow.
   - *Severity:* **high** (single address space; reachable by anyone in
     range, pre-authentication).
2. **Correct LE Secure Connections pairing** — ECDH P-256 with peer
   public-key validation, MITM protection under authenticated association
   models, fresh nonces, correct key derivation. *(inferred — §14 Q12)*
   - *Violation:* accepting an invalid/zero/debug ECDH key, nonce reuse,
     or a downgrade that silently drops MITM protection the app required.
   - *Severity:* **high** (breaks the security NimBLE exists to provide).
3. **Correct AES-CCM link encryption** once a key is established
   (`ble_ll_crypto`). *(inferred — §14 Q9)*
   - *Violation:* keystream/nonce reuse, encryption not actually applied
     after the start-encryption procedure.
   - *Severity:* **high**.
4. **Enforcement of the ATT permission the application set** on a
   characteristic (require-encryption / require-authentication). *(inferred
   — §14 Q11)*
   - *Violation:* serving a read/write on an encryption-required attribute
     over an unencrypted link.
   - *Severity:* **high**.
5. **Address privacy via RPA** when enabled. *(documented: `ble_hs_pvcy`)*
   - *Violation:* a static identifier leaking despite RPA being on.
   - *Severity:* **medium**.

## §9 Security properties the project does *not* provide

- **No security for unauthenticated association ("Just Works") pairing.**
  By spec, Just Works has **no MITM protection**; an active MITM can pair
  with both sides. The application must *choose* an authenticated method
  (Passkey / Numeric Comparison / OOB) for sensitive use. *(spec-defined;
  §11a)*
- **No confidentiality before encryption is established.** All Tier 0/1
  traffic is sniffable. *(spec-defined)*
- **No protection against jamming / RF DoS / connection flooding.**
  *(inferred — §14 Q14)*
- **No app-layer access control.** NimBLE enforces the *char permission
  the app set*; it does not decide policy. A characteristic the app left
  open is open by the app's choice. *(inferred — §14 Q11)*
- **No defence against a legitimately-bonded peer** doing what its bond
  permits. *(inferred)*
- **No guarantee about legacy pairing strength** — legacy pairing is weak
  by modern standards; use LE Secure Connections. *(spec-defined; §11a)*
- **No constant-time / side-channel guarantee** on SM crypto unless
  confirmed. *(inferred — §14 Q13)*

### False friends

- **"Paired" ≠ "authenticated."** A Just-Works bond is *paired* but not
  *MITM-protected*. The `authenticated` flag on the bond is the property
  that matters.
- **"Connected" ≠ "authorized."** Anyone in range can connect; access to
  data is gated by ATT permissions, not by the existence of a connection.
- **A resolvable private address still resolves** for a peer holding the
  IRK — RPA defeats *tracking by third parties*, not identification by a
  bonded peer.

## §10 Downstream responsibilities

1. **Require an authenticated pairing method** (Passkey / Numeric
   Comparison / OOB) before exposing anything sensitive — do not rely on
   Just Works for confidential or safety-relevant data.
2. **Set ATT characteristic permissions correctly** — mark sensitive
   characteristics require-encryption and/or require-authentication.
3. **Prefer LE Secure Connections; disable legacy pairing** where the
   threat model needs modern guarantees.
4. **Enable privacy (RPA)** if device tracking is a concern.
5. **Provision a strong platform RNG** and confidential bond storage.
6. **Keep test features (DTM) out of production** builds.

## §11 Known misuse patterns

- Using **Just Works** pairing for sensitive data and assuming it
  protects against MITM.
- Exposing sensitive GATT characteristics with **no encryption/auth
  permission** and assuming "you need our app to connect."
- Assuming **advertising is private** or that a static address is not
  trackable (use RPA).
- Relying on **legacy pairing** for modern confidentiality.
- Shipping with **Direct Test Mode** reachable.
- Treating a **connection** as if it were authorization.

## §11a Known non-findings (recurring false positives)

| Reported as | Why it is a non-finding | Cite |
| --- | --- | --- |
| "Device advertises / is connectable without authentication" | BLE advertising and connection establishment are unauthenticated *by spec*; data access is gated by ATT permissions, not by connecting. | §3.2, §9 |
| "Just Works pairing has no MITM protection" | True and *spec-defined*; the application must select an authenticated association model. Not a NimBLE defect. | §9, §10.1 |
| "Traffic is sniffable before encryption" | Spec-defined; confidentiality begins after the encryption-start procedure. | §9 |
| "Legacy pairing is cryptographically weak" | Spec property of legacy pairing; remediation is to require LE Secure Connections (which NimBLE supports). | §9, §10.3 |
| "A peer can reset/flood/jam the link (DoS)" | No availability guarantee against an in-range RF adversary is claimed. | §9 |
| "Static/public BD address is trackable" | RPA exists for exactly this; if disabled, that is the integrator's choice. | §9, §10.4 |
| "Characteristic X is readable by an unpaired peer" | That is the application's ATT-permission choice, not a NimBLE bug — *unless* NimBLE failed to enforce a permission that **was** set (then VALID). | §9, §10.2 |
| "ECDH debug keys accepted in test build" | Debug keys are a spec test feature; relevant only if reachable in a production build (→ §14 Q12). | §5a, §14 Q12 |

The discriminator: anything resting on **"BLE/this pairing mode is
unauthenticated"** is spec, not bug. A **memory-safety/bounds failure in
a parser**, or a **crypto/state-machine error that breaks a guarantee
NimBLE *does* claim (SM key validation, encryption actually applied,
enforcing an app-set permission)**, is VALID.

## §12 Conditions that would change this model

- A new pairing/association model or SM feature.
- A change to host/controller split or HCI handling.
- A new LL control procedure or PDU type from a spec update.
- Moving key storage in-tree, or taking ownership of the platform RNG.
- A new in-tree service that is more than a standard/demo profile.

## §13 Triage dispositions

| Disposition | Use when |
| --- | --- |
| **VALID** | Memory-unsafety/bounds failure in any LL/HCI/L2CAP/ATT/SM parser reachable from the radio; SM cryptographic/state-machine error (bad ECDH key accepted, nonce/key reuse, silent MITM-downgrade); encryption not applied after start; failure to enforce an ATT permission the app set. |
| **OUT-OF-MODEL** | The report depends on a spec-defined unauthenticated/weak behaviour (open advertising, Just Works, pre-encryption sniffing, legacy pairing) — §9. |
| **DOWNSTREAM** | Fix is the integrator's: require authenticated pairing, set char permissions, enable RPA, disable legacy/DTM (§10). |
| **NON-FINDING** | Matches a §11a row. |
| **MODEL-GAP** | In-scope in spirit but no §8/§9 item covers it → §14. |

## §14 Open questions for the maintainers

**Build / architecture**
- **Q1.** For split host/controller builds, is the HCI transport treated
  as a trust boundary (each side hardened against a hostile peer), or are
  both sides assumed cooperative?
- **Q2.** Where do bonds/keys persist, and is that store's confidentiality
  an in-model NimBLE property or the host's?
- **Q3.** Confirm the single-address-space / no-isolation amplification.

**Crypto / Security Manager (highest priority)**
- **Q4.** What RNG does SM/ECDH/RPA consume, and is its CSPRNG quality an
  assumption (platform's) or a NimBLE property?
- **Q5.** Which SM association models and features are enabled by default,
  and does NimBLE prevent a silent downgrade below the MITM/encryption
  level the application requested?
- **Q12.** In LE Secure Connections, does `ble_sm_sc` validate the peer's
  ECDH public key (reject invalid-curve / zero / known debug keys), and
  are nonces guaranteed fresh? (This is the single most important crypto
  question.)
- **Q13.** Any constant-time / side-channel posture for SM operations?

**Parser robustness**
- **Q6.** What bounds reassembly/allocation (MTU, COC buffers, connection
  count), and is a lying length field guaranteed to stay within them?
- **Q8.** Robustness target for malformed advertising data / connection
  PDUs at Tier 0?
- **Q9.** Handling of every LL control opcode, especially the
  encryption-start and length/PHY-update procedures?
- **Q10.** L2CAP reassembly and COC credit/flow bounds against a lying
  peer?
- **Q11.** Does `ble_att_svr` enforce per-attribute encryption/auth
  permissions, and what bounds the prepared-write queue (a known DoS
  sink)?

**Features**
- **Q7.** Is `ble_ll_dtm` (Direct Test Mode) reachable in production
  builds, or compile-gated to test?
- **Q14.** Any intended posture on connection-flood / resource-exhaustion
  DoS, or wholly out of scope?

**Meta**
- **Q15.** This repo has **no `SECURITY.md`/`AGENTS.md`** today. OK for
  this `THREAT_MODEL.md` to be the canonical model, reached via a new
  `AGENTS.md → SECURITY.md → THREAT_MODEL.md` chain (the discoverability
  PR we will open alongside)?
- **Q16.** Should the in-tree `host/services/*` profiles be explicitly
  in or out of this model (we currently treat them as standard/demo,
  §3.1)?

## Appendix: existing security-policy artefacts → §x back-map

At `master @ 793cb1612088`, `apache/mynewt-nimble` contains **no
`SECURITY.md`, `AGENTS.md`, or prior threat-model document**. The
`nimble/doc` and `qualification/` trees are Bluetooth-qualification and
API documentation, not a security policy. This is a greenfield v0; there
is nothing to supersede or back-map. Source-layout facts are cited inline
as *(documented)*.
