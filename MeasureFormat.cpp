#include "StdAfx.h"
#include "MeasureFormat.h"

namespace MeasureFormat
{

// Shared helper: format a double as "1,234.56" with the given decimal places.
static CString ThousandsSep(double value, int decimals)
{
    long long intPart  = static_cast<long long>(value);
    double    fracPart = value - static_cast<double>(intPart);

    CString intStr;
    intStr.Format(_T("%lld"), intPart);

    CString result;
    int len    = intStr.GetLength();
    int next   = len % 3;
    for (int i = 0; i < len; i++)
    {
        if (i > 0 && i == next) { result += _T(","); next += 3; }
        result += intStr[i];
    }

    if (decimals > 0)
    {
        double scale = 1.0;
        for (int i = 0; i < decimals; i++) scale *= 10.0;
        CString decStr;
        decStr.Format(_T(".%0*d"), decimals,
                      static_cast<int>(fracPart * scale + 0.5));
        result += decStr;
    }
    return result;
}

CString FormatArea(double area, AcDb::UnitsValue units)
{
    double  displayArea = area;
    CString suffix;

    switch (units)
    {
        case AcDb::kUnitsMillimeters:
            displayArea = area / 1000000.0;  // mm² → m²
            suffix      = _T(" m\u00B2");
            break;
        case AcDb::kUnitsCentimeters: suffix = _T(" cm\u00B2"); break;
        case AcDb::kUnitsMeters:      suffix = _T(" m\u00B2");  break;
        case AcDb::kUnitsKilometers:  suffix = _T(" km\u00B2"); break;
        case AcDb::kUnitsInches:      suffix = _T(" in\u00B2"); break;
        case AcDb::kUnitsFeet:        suffix = _T(" ft\u00B2"); break;
        case AcDb::kUnitsYards:       suffix = _T(" yd\u00B2"); break;
        case AcDb::kUnitsMiles:       suffix = _T(" mi\u00B2"); break;
        default:                      suffix = _T(" units\u00B2"); break;
    }

    return ThousandsSep(displayArea, 2) + suffix;
}

CString FormatLength(double length, AcDb::UnitsValue units,
                     bool rawUnits, bool noSuffix)
{
    double  displayLen = length;
    CString suffix;

    // Convert mm → m when not requesting raw units
    if (!rawUnits && units == AcDb::kUnitsMillimeters)
        displayLen = length / 1000.0;

    if (!noSuffix)
    {
        switch (units)
        {
            case AcDb::kUnitsMillimeters:
                suffix = rawUnits ? _T(" mm") : _T(" m"); break;
            case AcDb::kUnitsCentimeters: suffix = _T(" cm");    break;
            case AcDb::kUnitsMeters:      suffix = _T(" m");     break;
            case AcDb::kUnitsKilometers:  suffix = _T(" km");    break;
            case AcDb::kUnitsInches:      suffix = _T(" in");    break;
            case AcDb::kUnitsFeet:        suffix = _T(" ft");    break;
            case AcDb::kUnitsYards:       suffix = _T(" yd");    break;
            case AcDb::kUnitsMiles:       suffix = _T(" mi");    break;
            default:                      suffix = _T(" units"); break;
        }
    }

    return ThousandsSep(displayLen, 2) + suffix;
}

} // namespace MeasureFormat
