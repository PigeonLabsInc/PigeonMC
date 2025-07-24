package com.icelater.anticheat.checks;

import org.bukkit.entity.Player;
import org.bukkit.event.EventHandler;
import org.bukkit.event.player.EntityDamageEvent;

public class NoFallCheck implements Check {
    private int threshold;

    @Override
    public String getName() { return "NoFallCheck"; }

    @Override
    public boolean isEnabled() { return threshold > 0; }

    @Override
    public void configure(CheckConfig config) {
        this.threshold = config.get("threshold", Integer.class);
    }

    @EventHandler
    public void onDamage(EntityDamageEvent e) {
        if (!(e.getEntity() instanceof Player)) return;
        Player p = (Player) e.getEntity();
        if (p.hasPermission("anticheat.bypass")) return;
        if (e.getCause() == EntityDamageEvent.DamageCause.FALL) {
            CheckManagerUtils.flag(p, getName());
            e.setCancelled(true);
        }
    }
}
