#pragma once

#include <vector>

/**
    The symmetric eigenproblem, in double: Householder reduction to
    tridiagonal form and the implicit QL algorithm (EISPACK's tred2 and tql2,
    in the arrangement of the public-domain JAMA package). Eigenvalues come
    back ascending, eigenvectors as the columns of V (row-major, n x n).

    Used once per change of Shape or Material, on matrices of a few hundred
    rows: about 20 ms here. Nothing per frame.
*/
namespace eido::eigen
{

/// a: the symmetric matrix, row-major n x n (only read). values: n
/// eigenvalues ascending. vectors: n x n, column k is the k-th eigenvector.
void Symmetric( const std::vector< double >& a, int n, std::vector< double >& values, std::vector< double >& vectors );

/// The generalised problem K x = w M x, M symmetric positive definite, by
/// Cholesky reduction (M = L L^T, L^-1 K L^-T). For the harness's polar
/// Ritz solve of the circle. Returns false if M is not positive definite.
bool Generalised( const std::vector< double >& k, const std::vector< double >& m, int n, std::vector< double >& values );

} // namespace eido::eigen
