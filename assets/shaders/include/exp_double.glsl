// Shared double exponential for non-positive kernel exponents.
// exp for x <= 0: Cody-Waite reduction by ln 2 and a degree-13 Taylor polynomial on
// |r| <= ln2/2 (truncation below 5e-18 relative); GLSL has no double-precision exp.
double ExpNonPositive(double x)
{
    // Resident scalar analysis rejects x < -708; other consumers retain the full tail.
    if (x < -745.2lf) return 0.0lf;
    const double invLn2 = 1.44269504088896338700e+00lf;
    const double ln2Hi = 6.93147180369123816490e-01lf, ln2Lo = 1.90821492927058770002e-10lf;
    precise double n = floor(x * invLn2 + 0.5lf);
    precise double r = (x - n * ln2Hi) - n * ln2Lo;
    precise double p = 1.0lf / 6227020800.0lf;
    p = p * r + 1.0lf / 479001600.0lf;
    p = p * r + 1.0lf / 39916800.0lf;
    p = p * r + 1.0lf / 3628800.0lf;
    p = p * r + 1.0lf / 362880.0lf;
    p = p * r + 1.0lf / 40320.0lf;
    p = p * r + 1.0lf / 5040.0lf;
    p = p * r + 1.0lf / 720.0lf;
    p = p * r + 1.0lf / 120.0lf;
    p = p * r + 1.0lf / 24.0lf;
    p = p * r + 1.0lf / 6.0lf;
    p = p * r + 0.5lf;
    p = p * r + 1.0lf;
    p = p * r + 1.0lf;
    return ldexp(p, int(n));
}
