// =============================================================================
// PROJECT CHRONO - http://projectchrono.org
//
// Copyright (c) 2014 projectchrono.org
// All rights reserved.
//
// Use of this source code is governed by a BSD-style license that can be found
// in the LICENSE file at the top level of the distribution and at
// http://projectchrono.org/license-chrono.txt.
//
// =============================================================================
// Authors: Radu Serban
// =============================================================================
//
// Base class for a sprocket template with gear profile composed of circular
// arcs, suitable for interaction with single-pin track shoes.
//
// =============================================================================

#include <cmath>

#include "chrono_vehicle/tracked_vehicle/sprocket/ChSprocketSinglePin.h"
#include "chrono_vehicle/tracked_vehicle/track_shoe/ChTrackShoeSinglePin.h"
#include "chrono_vehicle/tracked_vehicle/ChTrackAssembly.h"


namespace chrono {
namespace vehicle {

// -----------------------------------------------------------------------------
// -----------------------------------------------------------------------------
class SprocketSinglePinContactCB : public ChSystem::CustomCollisionCallback {
  public:
    SprocketSinglePinContactCB(ChTrackAssembly* track,     ///< containing track assembly
                               double envelope,            ///< collision detection envelope
                               int gear_nteeth,            ///< number of teeth of the sprocket gear
                               double gear_RO,             ///< radius if the addendum circle
                               double gear_RC,             ///< radius of the arc centers circle
                               double gear_R,              ///< radius of the tooth arc profile
                               double separation,          ///< separation between sprocket gears
                               double shoe_locF,           ///< location of front cylinder on shoe (in local frame)
                               double shoe_locR,           ///< location of rear cylinder on shoe (in local frame)
                               double shoe_R,              ///< radius of shoe cylinders
                               bool lateral_contact,       ///< if true, enable lateral contact
                               double lateral_backlash,    ///< play relative to shoe guiding pin
                               const ChVector<>& shoe_pin  ///< location of shoe guide pin center
                               )
        : m_track(track),
          m_envelope(envelope),
          m_gear_nteeth(gear_nteeth),
          m_gear_RO(gear_RO),
          m_gear_RC(gear_RC),
          m_gear_R(gear_R),
          m_gear_Rhat(gear_R - envelope),
          m_separation(separation),
          m_shoe_locF(shoe_locF),
          m_shoe_locR(shoe_locR),
          m_shoe_R(shoe_R),
          m_lateral_contact(lateral_contact),
          m_lateral_backlash(lateral_backlash),
          m_shoe_pin(shoe_pin),
          m_shoe_Rhat(shoe_R + envelope) {
        m_sprocket = static_cast<ChSprocketSinglePin*>(m_track->GetSprocket().get());

        double safety_factor = 2;
        m_R_sum = m_gear_RO + m_shoe_R + safety_factor * m_envelope;
        m_R_diff = m_gear_R - m_shoe_R;
        m_Rhat_diff = m_gear_Rhat - m_shoe_Rhat;

        // Create contact material for sprocket - guiding pin contacts (to prevent detracking)
        // Note: zero friction
        ChContactMaterialData minfo;
        minfo.mu = 0;
        minfo.cr = 0.1f;
        minfo.Y = 1e7f;
        m_material = minfo.CreateMaterial(m_sprocket->GetGearBody()->GetSystem()->GetContactMethod());
    }

    virtual void OnCustomCollision(ChSystem* system) override;

  private:
    // Test collision of a shoe contact cylinder with the sprocket's gear profiles.
    // This may introduce up to two contacts (one with each gear plane).
    void CheckCylinderSprocket(std::shared_ptr<ChTrackShoeSinglePin> shoe,  // track shoe
                               const ChVector<>& locC_abs,  // center of shoe contact cylinder (global frame)
                               const ChVector<>& dirC_abs,  // direction of shoe contact cylinder (global frame)
                               const ChVector<> locS_abs    // center of sprocket (global frame)
    );

    // Test collision of a shoe contact circle with a gear plane profile.
    // This may introduce one contact.
    void CheckCircleProfile(std::shared_ptr<ChTrackShoeSinglePin> shoe,  // track shoe
                            const ChVector<>& loc                        // shoe contact circle center (sprocket frame)
    );

    // Test collision of a shoe guiding pin with the sprocket gear.
    // This may introduce one contact.
    void CheckPinSprocket(std::shared_ptr<ChTrackShoeSinglePin> shoe,  // track shoe
                          const ChVector<>& locPin_abs,                // center of guiding pin (global frame)
                          const ChVector<>& dirS_abs                   // sprocket Y direction (global frame)
    );

    // Find the center of the profile arc that is closest to the specified location.
    // The calculation is performed in the (x-z) plane.
    ChVector<> FindClosestArc(const ChVector<>& loc);

    ChTrackAssembly* m_track;         // pointer to containing track assembly
    ChSprocketSinglePin* m_sprocket;  // handle to the sprocket

    double m_envelope;  // collision detection envelope

    int m_gear_nteeth;    // sprocket gear, number of teeth
    double m_gear_RO;     // sprocket gear, outer radius (radius of addendum circle)
    double m_gear_RC;     // sprocket gear, radius of arc centers
    double m_gear_R;      // sprocket gear, arc radius
    double m_separation;  // separation distance between sprocket gears
    double m_shoe_locF;   // single-pin shoe, location of front contact cylinder
    double m_shoe_locR;   // single-pin shoe, location of rear contact cylinder
    double m_shoe_R;      // single-pin shoe, radius of contact cylinders

    bool m_lateral_contact;     // if true, generate lateral contacts
    double m_lateral_backlash;  // backlash relative to shoe guiding pin
    ChVector<> m_shoe_pin;      // single-pin shoe, center of guiding pin

    double m_gear_Rhat;  // adjusted gear arc radius
    double m_shoe_Rhat;  // adjusted she cylinder radius

    double m_R_sum;      // test quantity for broadphase check
    double m_R_diff;     // test quantity for narrowphase check
    double m_Rhat_diff;  // test quantity for narrowphase check

    std::shared_ptr<ChMaterialSurface> m_material;  // material for sprocket-pin contact (detracking)
};

void SprocketSinglePinContactCB::OnCustomCollision(ChSystem* system) {
    // Return now if collision disabled on sprocket or track shoes.
    if (m_track->GetNumTrackShoes() == 0)
        return;
    if (!m_sprocket->GetGearBody()->GetCollide() || !m_track->GetTrackShoe(0)->GetShoeBody()->GetCollide())
        return;

    // Sprocket gear center location, expressed in global frame
    ChVector<> locS_abs = m_sprocket->GetGearBody()->GetPos();

    // Sprocket "normal" (Y axis), expressed in global frame
    ChVector<> dirS_abs = m_sprocket->GetGearBody()->GetA().Get_A_Yaxis();

    // Loop over all shoes in the associated track
    for (size_t is = 0; is < m_track->GetNumTrackShoes(); ++is) {
        auto shoe = std::static_pointer_cast<ChTrackShoeSinglePin>(m_track->GetTrackShoe(is));

        // Calculate locations of the centers of the shoe's contact cylinders
        // (expressed in the global frame)
        ChVector<> locF_abs = shoe->GetShoeBody()->TransformPointLocalToParent(ChVector<>(m_shoe_locF, 0, 0));
        ChVector<> locR_abs = shoe->GetShoeBody()->TransformPointLocalToParent(ChVector<>(m_shoe_locR, 0, 0));

        // Express contact cylinder direction (common for both cylinders) in the global frame
        ChVector<> dir_abs = shoe->GetShoeBody()->GetA().Get_A_Yaxis();

        // Perform collision test for the front contact cylinder
        CheckCylinderSprocket(shoe, locF_abs, dir_abs, locS_abs);

        // Perform collision test for the rear contact cylinder.
        CheckCylinderSprocket(shoe, locR_abs, dir_abs, locS_abs);

        if (m_lateral_contact) {
            // Express guiding pin center in the global frame
            ChVector<> locPin_abs = shoe->GetShoeBody()->TransformPointLocalToParent(m_shoe_pin);

            // Perform collision detection with the central pin
            CheckPinSprocket(shoe, locPin_abs, dirS_abs);
        }
    }
}

// Perform collision test between one of the shoe's contact cylinders and the
// sprocket gear profiles.
void SprocketSinglePinContactCB::CheckCylinderSprocket(std::shared_ptr<ChTrackShoeSinglePin> shoe,
                                                       const ChVector<>& locC_abs,
                                                       const ChVector<>& dirC_abs,
                                                       const ChVector<> locS_abs) {
    // Broadphase collision test: no contact if the cylinder center is too far from
    // the sprocket center.
    if ((locC_abs - locS_abs).Length2() > m_R_sum * m_R_sum)
        return;

    // Express the center of the contact cylinder and its direction in the sprocket frame.
    ChVector<> locC = m_sprocket->GetGearBody()->TransformPointParentToLocal(locC_abs);
    ChVector<> dirC = m_sprocket->GetGearBody()->TransformDirectionParentToLocal(dirC_abs);

    // Sanity check: the cylinder must intersect the gear planes.
    assert(dirC.y() != 0);

    // Working in the sprocket frame, intersect the contact cylinder with the two
    // ("positive" and "negative") gear planes.
    double alphaP = (0.5 * m_separation - locC.y()) / dirC.y();
    double alphaN = (-0.5 * m_separation - locC.y()) / dirC.y();
    ChVector<> locP = locC + alphaP * dirC;
    ChVector<> locN = locC + alphaN * dirC;

    // Perform collision test with the "positive" gear profile.
    CheckCircleProfile(shoe, locP);

    // Perform collision test with the "negative" gear profile.
    CheckCircleProfile(shoe, locN);
}

// Working in the (x-z) plane of the gear, perform a 2D collision test between the
// gear profile and a circle centered at the specified location.
void SprocketSinglePinContactCB::CheckCircleProfile(std::shared_ptr<ChTrackShoeSinglePin> shoe, const ChVector<>& loc) {
    // No contact if the circle center is too far from the gear center.
    if (loc.x() * loc.x() + loc.z() * loc.z() > m_gear_RC * m_gear_RC)
        return;

    // Find the candidate profile arc center.
    ChVector<> center = FindClosestArc(loc);

    // Test contact between the shoe circle (convex) and the gear arc (concave).
    // Note that we use the adjusted arc and cylinder radii (adjusted by the envelope).
    ChVector<> delta = center - loc;
    double dist2 = delta.Length2();

    // If the two centers (circle and arc) are separated by less than the difference
    // of their adjusted radii, there is no contact.
    if (dist2 <= m_Rhat_diff * m_Rhat_diff)
        return;

    /*
    // Ignore contact if the distance between the centers is more than the arc radius.
    if (dist2 >= m_gear_R * m_gear_R)
        return;
    */

    // Generate contact information (still in the sprocket frame)
    double dist = std::sqrt(dist2);
    ChVector<> normal = delta / dist;
    ChVector<> pt_gear = center - m_gear_R * normal;
    ChVector<> pt_shoe = loc - m_shoe_R * normal;

    // Ignore contact if the contact point on the gear is above the outer radius
    if (pt_gear.x() * pt_gear.x() + pt_gear.z() * pt_gear.z() > m_gear_RO * m_gear_RO)
        return;

    // Fill in contact information and add the contact to the system.
    // Express all vectors in the global frame
    ChCollisionInfo contact;
    contact.modelA = m_sprocket->GetGearBody()->GetCollisionModel().get();
    contact.modelB = shoe->GetShoeBody()->GetCollisionModel().get();
    contact.shapeA = nullptr;
    contact.shapeB = nullptr;
    contact.vN = m_sprocket->GetGearBody()->TransformDirectionLocalToParent(normal);
    contact.vpA = m_sprocket->GetGearBody()->TransformPointLocalToParent(pt_gear);
    contact.vpB = m_sprocket->GetGearBody()->TransformPointLocalToParent(pt_shoe);
    contact.distance = m_R_diff - dist;
    ////contact.eff_radius = m_shoe_R;  //// TODO: take into account m_gear_R?

    m_sprocket->GetGearBody()->GetSystem()->GetContactContainer()->AddContact(contact, m_sprocket->GetContactMaterial(),
                                                                              shoe->GetSprocketContactMaterial());
}

// Find the center of the profile arc that is closest to the specified location.
// The calculation is performed in the (x-z) plane.
// It is assumed that the gear profile is specified with an arc at its lowest z value.
ChVector<> SprocketSinglePinContactCB::FindClosestArc(const ChVector<>& loc) {
    // Angle between two consecutive gear teeth
    double delta = CH_C_2PI / m_gear_nteeth;
    // Angle formed by 'loc' and the line z<0
    double angle = std::atan2(loc.x(), -loc.z());
    // Find angle of closest profile arc
    double arc_angle = delta * std::round(angle / delta);
    // Return the arc center location
    return ChVector<>(m_gear_RC * std::sin(arc_angle), loc.y(), -m_gear_RC * std::cos(arc_angle));
}

void SprocketSinglePinContactCB::CheckPinSprocket(std::shared_ptr<ChTrackShoeSinglePin> shoe,
                                                  const ChVector<>& locPin_abs,
                                                  const ChVector<>& dirS_abs) {
    // Express pin center in the sprocket frame
    ChVector<> locPin = m_sprocket->GetGearBody()->TransformPointParentToLocal(locPin_abs);

    // No contact if the pin is close enough to the sprocket's center
    if (std::abs(locPin.y()) < m_lateral_backlash)
        return;

    // No contact if pin is too far from sprocket center
    if (locPin.x() * locPin.x() + locPin.z() * locPin.z() > m_gear_RO * m_gear_RO)
        return;

    // Fill in contact information and add the contact to the system.
    // Express all vectors in the global frame
    ChCollisionInfo contact;
    contact.modelA = m_sprocket->GetGearBody()->GetCollisionModel().get();
    contact.modelB = shoe->GetShoeBody()->GetCollisionModel().get();
    contact.shapeA = nullptr;
    contact.shapeB = nullptr;
    if (locPin.y() < 0) {
        contact.distance = m_lateral_backlash + locPin.y();
        contact.vN = dirS_abs;
    } else {
        contact.distance = m_lateral_backlash - locPin.y();
        contact.vN = -dirS_abs;
    }
    contact.vpA = locPin_abs - contact.distance * contact.vN;
    contact.vpB = locPin_abs;

    ////std::cout << "CONTACT";
    ////std::cout << "  pin: " << locPin.y();
    ////std::cout << "  delta: " << contact.distance;
    ////std::cout << "  normal: " << contact.vN;
    ////std::cout << std::endl;

    m_sprocket->GetGearBody()->GetSystem()->GetContactContainer()->AddContact(contact, m_material, m_material);
}

// -----------------------------------------------------------------------------
// -----------------------------------------------------------------------------
ChSprocketSinglePin::ChSprocketSinglePin(const std::string& name) : ChSprocket(name) {
}

// -----------------------------------------------------------------------------
// -----------------------------------------------------------------------------
std::shared_ptr<ChSystem::CustomCollisionCallback> ChSprocketSinglePin::GetCollisionCallback(ChTrackAssembly* track) {
    // Check compatibility between this type of sprocket and the track shoes.
    // We expect track shoes of type ChSinglePinShoe.
    auto shoe = std::dynamic_pointer_cast<ChTrackShoeSinglePin>(track->GetTrackShoe(0));
    assert(shoe);

    // Extract parameterization of gear profile
    int gear_nteeth = GetNumTeeth();
    double gear_RO = GetOuterRadius();
    double gear_RC = GetArcCentersRadius();
    double gear_R = GetArcRadius();
    double lateral_backlash = GetLateralBacklash();

    // Extract parameterization of the shoe contact geometry.
    double shoe_locF = shoe->GetFrontCylinderLoc();
    double shoe_locR = shoe->GetRearCylinderLoc();
    double shoe_R = shoe->GetCylinderRadius();
    ChVector<> shoe_locPin = shoe->GetLateralContactPoint();

    // Create and return the callback object. Note: this pointer will be freed by the base class.
    return chrono_types::make_shared<SprocketSinglePinContactCB>(track, 0.005, gear_nteeth, gear_RO, gear_RC, gear_R,
                                                                 GetSeparation(), shoe_locF, shoe_locR, shoe_R,
                                                                 m_lateral_contact, lateral_backlash, shoe_locPin);
}

// -----------------------------------------------------------------------------
// Create and return the sprocket gear profile.
// -----------------------------------------------------------------------------
std::shared_ptr<geometry::ChLinePath> ChSprocketSinglePin::GetProfile() const {
    auto profile = chrono_types::make_shared<geometry::ChLinePath>();

    int num_teeth = GetNumTeeth();
    double R_T = GetOuterRadius();
    double R_C = GetArcCentersRadius();
    double R = GetArcRadius();

    double beta = CH_C_2PI / num_teeth;
    double sbeta = std::sin(beta / 2);
    double cbeta = std::cos(beta / 2);
    double y = (R_T * R_T + R_C * R_C - R * R) / (2 * R_C);
    double x = std::sqrt(R_T * R_T - y * y);
    double gamma = std::asin(x / R);

    for (int i = 0; i < num_teeth; ++i) {
        double alpha = CH_C_PI - i * beta;
        ChVector<> p0(0, R_C, 0);
        ChVector<> p1(-R_T * sbeta, R_T * cbeta, 0);
        ChVector<> p2(-x, y, 0);
        ChVector<> p3(x, y, 0);
        ChVector<> p4(R_T * sbeta, R_T * cbeta, 0);
        ChQuaternion<> quat;
        quat.Q_from_AngZ(alpha);
        ChMatrix33<> rot(quat);
        p0 = rot * p0;
        p1 = rot * p1;
        p2 = rot * p2;
        p3 = rot * p3;
        p4 = rot * p4;
        geometry::ChLineSegment seg1(p1, p2);
        double angle1 = alpha + 1.5 * CH_C_PI - gamma;
        double angle2 = alpha + 1.5 * CH_C_PI + gamma;
        geometry::ChLineArc arc(ChCoordsys<>(p0), R, angle1, angle2, true);
        geometry::ChLineSegment seg2(p3, p4);
        profile->AddSubLine(seg1);
        profile->AddSubLine(arc);
        profile->AddSubLine(seg2);
    }

    return profile;
}

}  // end namespace vehicle
}  // end namespace chrono
