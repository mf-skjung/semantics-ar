namespace SemanticsAr.Core.Domain;

public sealed record RestoreRequest(RecoverableItem Item, string TargetPath);
