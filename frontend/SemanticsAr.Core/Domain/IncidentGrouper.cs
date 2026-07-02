namespace SemanticsAr.Core.Domain;

public static class IncidentGrouper
{
    public static IReadOnlyList<Incident> Group(IEnumerable<IIncidentSource> records, TimeSpan adjacencyWindow)
    {
        ArgumentNullException.ThrowIfNull(records);
        if (adjacencyWindow < TimeSpan.Zero)
            throw new ArgumentOutOfRangeException(nameof(adjacencyWindow));

        List<IIncidentSource> eligible = records.Where(r => r.ActorStartKey != 0).ToList();
        int count = eligible.Count;
        if (count == 0)
            return [];

        int[] parent = new int[count];
        for (int i = 0; i < count; i++)
            parent[i] = i;

        for (int i = 0; i < count; i++)
        {
            for (int j = i + 1; j < count; j++)
            {
                if (eligible[i].ActorStartKey != eligible[j].ActorStartKey)
                    continue;

                TimeSpan diff = eligible[i].Timestamp > eligible[j].Timestamp
                    ? eligible[i].Timestamp - eligible[j].Timestamp
                    : eligible[j].Timestamp - eligible[i].Timestamp;
                if (diff <= adjacencyWindow)
                    Union(parent, i, j);
            }
        }

        Dictionary<int, List<IIncidentSource>> groups = [];
        for (int i = 0; i < count; i++)
        {
            int root = Find(parent, i);
            if (!groups.TryGetValue(root, out List<IIncidentSource>? members))
            {
                members = [];
                groups[root] = members;
            }
            members.Add(eligible[i]);
        }

        List<Incident> incidents = new(groups.Count);
        foreach (List<IIncidentSource> members in groups.Values)
        {
            members.Sort((a, b) => a.Timestamp.CompareTo(b.Timestamp));
            incidents.Add(new Incident(
                members[0].ActorStartKey,
                members[0].Timestamp,
                members[^1].Timestamp,
                members));
        }

        incidents.Sort((a, b) => a.FirstSeen.CompareTo(b.FirstSeen));
        return incidents;
    }

    private static int Find(int[] parent, int x)
    {
        while (parent[x] != x)
        {
            parent[x] = parent[parent[x]];
            x = parent[x];
        }
        return x;
    }

    private static void Union(int[] parent, int a, int b)
    {
        int rootA = Find(parent, a);
        int rootB = Find(parent, b);
        if (rootA != rootB)
            parent[rootA] = rootB;
    }
}
