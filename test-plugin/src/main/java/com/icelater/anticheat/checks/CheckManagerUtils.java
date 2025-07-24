package com.icelater.anticheat.checks;

import com.icelater.anticheat.AdvancedAntiCheat;
import org.bukkit.BanList;
import org.bukkit.Bukkit;
import org.bukkit.entity.Player;

import java.util.HashMap;
import java.util.Map;
import java.util.UUID;

public class CheckManagerUtils {
    private static final Map<UUID, Integer> violations = new HashMap<>();
    private static final Map<UUID, Long> lastBreak = new HashMap<>();

    public static void flag(Player p, String check) {
        int count = violations.getOrDefault(p.getUniqueId(), 0) + 1;
        violations.put(p.getUniqueId(), count);
        AdvancedAntiCheat.getInstance().getLogger().warning(p.getName() + " failed " + check + " (count=" + count + ")");
        int banThreshold = AdvancedAntiCheat.getInstance().getConfig().getInt("ban-threshold");
        if (AdvancedAntiCheat.getInstance().getConfig().getBoolean("ban-on-violation") && count >= banThreshold) {
            Bukkit.getBanList(BanList.Type.NAME).addBan(p.getName(), AdvancedAntiCheat.getInstance().getConfig().getString("ban-message"), null, null);
            p.kickPlayer(AdvancedAntiCheat.getInstance().getConfig().getString("ban-message"));
        }
    }

    public static long getLastBreak(Player p) {
        return lastBreak.getOrDefault(p.getUniqueId(), 0L);
    }

    public static void updateLastBreak(Player p, long time) {
        lastBreak.put(p.getUniqueId(), time);
    }
          }
