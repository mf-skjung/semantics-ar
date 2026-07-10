using CommunityToolkit.Mvvm.ComponentModel;
using SemanticsAr.Core.Domain;
using Chip = SemanticsAr.App.Controls.CertaintyRung;

namespace SemanticsAr.App.ViewModels;

public partial class RecoverableItemViewModel : ObservableObject
{
    public RecoverableItemViewModel(RecoverableItem model, RestoreDisposition disposition, string groupLabel, bool canOverwriteInPlace)
    {
        Model = model;
        Disposition = disposition;
        GroupLabel = groupLabel;
        CanOverwriteInPlace = canOverwriteInPlace;
        _isSelected = disposition is RestoreDisposition.Additive or RestoreDisposition.DeletedSince;
    }

    public RecoverableItem Model { get; }

    public RestoreDisposition Disposition { get; }

    public string GroupLabel { get; }

    public bool CanOverwriteInPlace { get; }

    [ObservableProperty]
    private bool _isSelected;

    [ObservableProperty]
    private bool _overwriteInPlace;

    public bool CanSelect => Disposition != RestoreDisposition.Blocked;

    public Chip Rung => Model.Rung switch
    {
        CertaintyRung.Definitive => Chip.Definitive,
        CertaintyRung.Bounded => Chip.Bounded,
        _ => Chip.Unrecoverable,
    };

    public string DisplayPath => Model.ProvenancePath;

    public string Detail => Model.Rung == CertaintyRung.Definitive
        ? "The captured key restores the original in place — your file is replaced only after byte-for-byte verification, and left untouched if it cannot be verified."
        : Disposition switch
        {
            RestoreDisposition.Additive =>
                "Unchanged since it was captured — restored as a separate copy beside your current file.",
            RestoreDisposition.DeletedSince =>
                "Deleted since it was captured — restored in place as a new file.",
            RestoreDisposition.ModifiedSince =>
                "Changed since it was captured — restored beside your current file, keeping both.",
            _ =>
                "A folder now occupies this path — it cannot be restored here.",
        };
}
