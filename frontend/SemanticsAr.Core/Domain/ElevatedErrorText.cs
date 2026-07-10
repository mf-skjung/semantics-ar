namespace SemanticsAr.Core.Domain;

public static class ElevatedErrorText
{
    public static string Describe(ElevatedError error, string action) => error switch
    {
        ElevatedError.AccessDenied =>
            $"Elevation is required to {action}.",
        ElevatedError.PipeUnavailable =>
            "The protection service cannot be reached right now.",
        ElevatedError.ServerUntrusted =>
            "The control channel could not be verified as genuine. This action was refused.",
        ElevatedError.VersionMismatch =>
            "The app and the protection components are different versions.",
        ElevatedError.Transport =>
            $"The control channel failed while trying to {action}.",
        _ =>
            $"Could not {action} right now.",
    };
}
