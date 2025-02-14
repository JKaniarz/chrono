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
// Definitions of the classes that implement a Bezier 3D path.
//
// ChBezierCurve
//    This class encapsulates a piece-wise cubic Bezier approximation of a
//    3D curve, represented as a set of three arrays of locations. For each
//    point on the curve, we also define a vector 'inCV' which represents the
//    vertex of the control polygon prior to the point and a vector 'outCV'
//    which represents the vertex of the control polygon following the point.
//    This class provides methods for evaluating the value, as well as the
//    first and second derivatives of a point on a specified interval of the
//    piece-wise 3D curve (using the Bernstein polynomial representation of
//    Bezier curves). In addition, it provides a method for calculating the
//    closest point on a specified interval of the curve to a specified
//    location.
//
// ChBezierCurveTracker
//    This utility class implements a tracker for a given path. It uses time
//    coherence in order to provide an appropriate initial guess for the
//    iterative (Newton) root finder.
//
// =============================================================================

#ifndef CH_BEZIER_CURVE_H
#define CH_BEZIER_CURVE_H

#include <string>
#include <vector>

#include "chrono/core/ChApiCE.h"
#include "chrono/core/ChFrame.h"
#include "chrono/core/ChVector.h"
#include "chrono/serialization/ChArchive.h"

namespace chrono {

// -----------------------------------------------------------------------------
/// Definition of a piece-wise cubic Bezier approximation of a 3D curve.
///
/// This class encapsulates a piece-wise cubic Bezier approximation of a
/// 3D curve, represented as a set of three arrays of locations. For each
/// point on the curve, we also define a vector 'inCV' which represents the
/// vertex of the control polygon prior to the point and a vector 'outCV'
/// which represents the vertex of the control polygon following the point.
/// This class provides methods for evaluating the value, as well as the
/// first and second derivatives of a point on a specified interval of the
/// piece-wise 3D curve (using the Bernstein polynomial representation of
/// Bezier curves). In addition, it provides a method for calculating the
/// closest point on a specified interval of the curve to a specified
/// location.
// -----------------------------------------------------------------------------
class ChApi ChBezierCurve {
  public:
    /// Constructor from specified nodes and control points.
    ChBezierCurve(const std::vector<ChVector<> >& points,
                  const std::vector<ChVector<> >& inCV,
                  const std::vector<ChVector<> >& outCV,
                  bool closed = false);

    /// Constructor from specified nodes.
    /// In this case, we evaluate the control polygon vertices inCV and outCV
    /// so that we obtain a piecewise cubic spline interpolant of the given knots.
    ChBezierCurve(const std::vector<ChVector<> >& points, bool closed = false);

    /// Default constructor (required by serialization)
    ChBezierCurve() {}

    /// Destructor for ChBezierCurve.
    ~ChBezierCurve() {}

    /// Set the nodes and control points
    void setPoints(const std::vector<ChVector<> >& points,
                   const std::vector<ChVector<> >& inCV,
                   const std::vector<ChVector<> >& outCV);

    /// Return the number of knot points.
    size_t getNumPoints() const { return m_points.size(); }

    /// Return the number of intervals (segments).
    size_t getNumSegments() const { return getNumPoints() - 1; }

    /// Return true if path is closed and false otherwise.
    bool IsClosed() const { return m_closed; }

    /// Return the knot point with specified index.
    const ChVector<>& getPoint(size_t i) const { return m_points[i]; }

    /// Return all curve knots.
    const std::vector<ChVector<>> getPoints() const { return m_points; }

    /// Evaluate the value of the Bezier curve.
    /// This function calculates and returns the point on the curve at the
    /// given curve parameter (assumed to be in [0,1]).  
    /// A value t=0 returns the first point on the curve.
    /// A value t=1 returns the last point on the curve.
    ChVector<> eval(double t) const;

    /// Evaluate the value of the Bezier curve.
    /// This function calculates and returns the point on the curve in the
    /// specified interval between two knot points and at the given curve
    /// parameter (assumed to be in [0,1]).
    /// A value t-0 returns the first end of the specified interval.
    /// A value t=1 return the second end of the specified interval.
    /// It uses the Bernstein polynomial representation of a Bezier curve.
    ChVector<> eval(size_t i, double t) const;

    /// Evaluate the tangent vector to the Bezier curve.
    /// This function calculates and returns the first derivative (tangent vector)
    /// to the curve in the specified interval between two knot points and at the
    /// given curve parameter (assumed to be in [0,1]). It uses the Bernstein
    /// polynomial representation of a Bezier curve.
    ChVector<> evalD(size_t i, double t) const;

    /// Evaluate the second derivative vector to the Bezier curve.
    /// This function calculates and returns the second derivative vector to the
    /// curve in the specified interval between two knot points and at the given
    /// curve parameter (assumed to be in [0,1]). It uses the Bernstein polynomial
    /// representation of a Bezier curve.
    ChVector<> evalDD(size_t i, double t) const;

    /// Calculate the closest point on the curve to the given location.
    /// This function calculates and returns the point on the curve in the specified
    /// interval that is closest to the specified location. On input, the value 't' is
    /// an initial guess. On return, it contains the curve parameter corresponding
    /// to the closest point.
    ChVector<> calcClosestPoint(const ChVector<>& loc, size_t i, double& t) const;

    /// Write the knots and control points to the specified file.
    void write(const std::string& filename);

    /// Create a ChBezierCurve using data in the specified file.
    /// The input file is assumed to contain on the first line the number of data points and the number of data columns.
    /// The latter can be one of 3 or 9. In the first case, subsequent lines should contain the coordinates of the curve
    /// knots (one point per line). The returned Bezier curve is a piecewise cubic spline through the specified points.
    /// In the second case, subsequent lines should contain the coordinates of the curve knot, the coordinates of the
    /// "incoming" control point, and the coordinates of the "outgoing" control point (i.e. 9 values per line). The
    /// returned curve is a general Bezier curve using the specified knots and control polygons.
    static std::shared_ptr<ChBezierCurve> read(const std::string& filename, bool closed = false);

    // SERIALIZATION

    /// Method to allow serialization of transient data to archives.
    void ArchiveOut(ChArchiveOut& marchive);

    /// Method to allow de-serialization of transient data from archives.
    void ArchiveIn(ChArchiveIn& marchive);

  private:
    /// Utility function to solve for the outCV control points.
    /// This function solves the resulting tridiagonal system for one of the
    /// coordinates (x, y, or z) of the outCV control points, to impose that the
    /// resulting Bezier curve is a spline interpolant of the knots.
    static void solveTriDiag(size_t n, double* rhs, double* x);

    std::vector<ChVector<> > m_points;  ///< set of knot points
    std::vector<ChVector<> > m_inCV;    ///< set on "incident" control points
    std::vector<ChVector<> > m_outCV;   ///< set of "outgoing" control points

    bool m_closed;  ///< treat the path as a closed loop curve

    static const size_t m_maxNumIters;  ///< maximum number of Newton iterations
    static const double m_sqrDistTol;   ///< tolerance on squared distance
    static const double m_cosAngleTol;  ///< tolerance for orthogonality test
    static const double m_paramTol;     ///< tolerance for change in parameter value

    friend class ChBezierCurveTracker;
};

// -----------------------------------------------------------------------------
/// Definition of a tracker on a ChBezierCurve path.
///
/// This utility class implements a tracker for a given path. It uses time
/// coherence in order to provide an appropriate initial guess for the
/// iterative (Newton) root finder.
// -----------------------------------------------------------------------------
class ChApi ChBezierCurveTracker {
  public:
    /// Create a tracker associated with the specified Bezier curve.
    ChBezierCurveTracker(std::shared_ptr<ChBezierCurve> path);

    /// Destructor for ChBezierCurveTracker.
    ~ChBezierCurveTracker() {}

    /// Reset the tracker at the specified location.
    /// This function reinitializes the pathTracker at the specified location. It
    /// calculates an appropriate initial guess for the curve segment and sets the
    /// curve parameter to 0.5.
    void reset(const ChVector<>& loc);

    /// Calculate the closest point on the underlying curve to the specified location.
    /// This function returns the closest point on the underlying path to the
    /// specified location. The return value is -1 if this point coincides with the
    /// first point of the path, +1 if it coincides with the last point of the path,
    /// and 0 otherwise. Note that, in order to provide a reasonable initial guess
    /// for the Newton iteration, we use time coherence (by keeping track of the path
    /// interval and curve parameter within that interval from the last query). As
    /// such, this function should be called with a continuous sequence of locations.
    int calcClosestPoint(const ChVector<>& loc, ChVector<>& point);

    /// Calculate the closest point on the underlying curve to the specified location.
    /// Return the TNB (tangent-normal-binormal) frame and the curvature at the closest point.
    /// The ChFrame 'tnb' has X axis along the tangent, Y axis along the normal, and Z axis
    /// along the binormal.  The frame location is the closest point on the Bezier curve.
    /// Note that the normal and binormal are not defined at points with zero curvature.
    /// In such cases, we return an orthonormal frame with X axis along the tangent.
    int calcClosestPoint(const ChVector<>& loc, ChFrame<>& tnb, double& curvature);

  private:
    std::shared_ptr<ChBezierCurve> m_path;  ///< associated Bezier curve
    size_t m_curInterval;                   ///< current search interval
    double m_curParam;                      ///< parameter for current closest point
};

CH_CLASS_VERSION(ChBezierCurve,0)

}  // end of namespace chrono

#endif
