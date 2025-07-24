package com.icelater.anticheat.checks;

import org.bukkit.entity.Player;
import org.bukkit.event.EventHandler;
import org.bukkit.event.player.PlayerMoveEvent;
import org.bukkit.util.Vector;

public class SpeedCheck implements Check {
    private double maxDistance;
    private int threshold;

    @Override
    public String getName() { return "SpeedCheck"; }

    @Override
    public boolean isEnabled() { return maxDistance > 0; }

    @Override
    public void configure(CheckConfig config) {
        this.maxDistance = config.get("max-distance", Double.class);
        this.threshold = config.get("threshold", Integer.class);
    }

    @EventHandler
    public void onMove(PlayerMoveEvent e) {
        Player p = e.getPlayer();
        if (p.hasPermission("anticheat.bypass")) return;
        Vector from = e.getFrom().toVector(), to = e.getTo().toVector();
        double distance = from.distance(to);
        if (distance > maxDistance) {
            CheckManagerUtils.flag(p, getName());
            e.setCancelled(true);
            p.teleport(e.getFrom());
        }
    }
}
