using SemanticsAr.Core.Domain;

namespace SemanticsAr.Core.Services;

public interface IElevatedControlChannel : IDisposable
{
    ElevatedError LoadCatalog(out IReadOnlyList<RecoverableItem> items);

    ElevatedError LoadPreserved(out IReadOnlyList<RecoverableItem> items);

    ElevatedError LoadAppIdentities(out IReadOnlyList<AppIdentity> items);

    ElevatedError LoadExemptions(out IReadOnlyList<Exemption> items);

    RecoveryOutcome Recover(RecoverableItem item, string targetPath);

    ElevatedError SetMode(uint mode);

    ElevatedError SetBudget(ulong retention100ns, ulong capacityBytes);

    ElevatedError ResolveIdentity(string imagePath, out ResolvedIdentity? identity);

    ExemptionAdd WhitelistAdd(string imagePath);

    ElevatedError WhitelistRemove(string imagePath, out uint verdict);
}
