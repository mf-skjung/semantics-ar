using SemanticsAr.Core.Domain;
using Xunit;

namespace SemanticsAr.Core.Tests;

public sealed class IncidentGrouperTests
{
    private static readonly DateTimeOffset Base = new(2026, 7, 3, 0, 0, 0, TimeSpan.Zero);

    private static IIncidentSource Ev(ulong actor, int offsetSeconds, JournalEventClass cls = JournalEventClass.KeyCaptured)
    {
        return new JournalEvent(cls, 1ul, (ulong)(offsetSeconds + 1), Base.AddSeconds(offsetSeconds), actor, false);
    }

    [Fact]
    public void EmptyInput_ProducesNoIncidents()
    {
        IReadOnlyList<Incident> incidents = IncidentGrouper.Group([], TimeSpan.FromMinutes(5));
        Assert.Empty(incidents);
    }

    [Fact]
    public void SameActor_WithinWindow_GroupsIntoOneIncident()
    {
        IIncidentSource[] events = [Ev(1ul, 0), Ev(1ul, 30), Ev(1ul, 60)];

        IReadOnlyList<Incident> incidents = IncidentGrouper.Group(events, TimeSpan.FromMinutes(2));

        Assert.Single(incidents);
        Assert.Equal(3, incidents[0].Members.Count);
        Assert.Equal(1ul, incidents[0].ActorStartKey);
    }

    [Fact]
    public void SameActor_OutsideWindow_SplitsIntoSeparateIncidents()
    {
        IIncidentSource[] events = [Ev(1ul, 0), Ev(1ul, 3600)];

        IReadOnlyList<Incident> incidents = IncidentGrouper.Group(events, TimeSpan.FromMinutes(5));

        Assert.Equal(2, incidents.Count);
    }

    [Fact]
    public void DifferentActors_NeverGroupTogether()
    {
        IIncidentSource[] events = [Ev(1ul, 0), Ev(2ul, 1)];

        IReadOnlyList<Incident> incidents = IncidentGrouper.Group(events, TimeSpan.FromMinutes(5));

        Assert.Equal(2, incidents.Count);
        Assert.Contains(incidents, i => i.ActorStartKey == 1ul);
        Assert.Contains(incidents, i => i.ActorStartKey == 2ul);
    }

    [Fact]
    public void ZeroActorStartKey_IsExcluded()
    {
        IIncidentSource[] events = [Ev(0ul, 0, JournalEventClass.ModeChanged), Ev(1ul, 1)];

        IReadOnlyList<Incident> incidents = IncidentGrouper.Group(events, TimeSpan.FromMinutes(5));

        Assert.Single(incidents);
        Assert.Equal(1ul, incidents[0].ActorStartKey);
    }

    [Fact]
    public void ChainedAdjacency_TransitivelyGroupsAcrossTheWindow()
    {
        IIncidentSource[] events = [Ev(1ul, 0), Ev(1ul, 90), Ev(1ul, 180)];

        IReadOnlyList<Incident> incidents = IncidentGrouper.Group(events, TimeSpan.FromSeconds(100));

        Assert.Single(incidents);
        Assert.Equal(3, incidents[0].Members.Count);
    }

    [Fact]
    public void Incident_ReportsFirstSeenAndLastSeenFromSortedMembers()
    {
        IIncidentSource[] events = [Ev(1ul, 60), Ev(1ul, 0), Ev(1ul, 30)];

        IReadOnlyList<Incident> incidents = IncidentGrouper.Group(events, TimeSpan.FromMinutes(5));

        Assert.Single(incidents);
        Assert.Equal(Base, incidents[0].FirstSeen);
        Assert.Equal(Base.AddSeconds(60), incidents[0].LastSeen);
    }

    [Fact]
    public void NegativeAdjacencyWindow_Throws()
    {
        Assert.Throws<ArgumentOutOfRangeException>(() =>
            IncidentGrouper.Group([], TimeSpan.FromSeconds(-1)));
    }
}
