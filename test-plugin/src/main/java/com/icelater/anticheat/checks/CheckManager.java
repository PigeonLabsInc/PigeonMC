package com.icelater.anticheat.checks;

import com.icelater.anticheat.AdvancedAntiCheat;
import org.bukkit.Bukkit;

import java.util.HashMap;
import java.util.Map;

public class CheckManager {
    private final AdvancedAntiCheat plugin;
    private final Map<String, Check> checks = new HashMap<>();

    public CheckManager(AdvancedAntiCheat plugin) {
        this.plugin = plugin;
    }

    public void register(Check check) {
        checks.put(check.getName(), check);
        Bukkit.getPluginManager().registerEvents(check, plugin);
    }

    public void registerAll() {
        plugin.getConfig().getConfigurationSection("checks").getKeys(false).forEach(name -> {
            try {
                Class<?> clazz = Class.forName("com.icelater.anticheat.checks." + name);
                Check check = (Check) clazz.getDeclaredConstructor().newInstance();
                check.configure(new CheckConfig(plugin.getConfig().getConfigurationSection("checks." + name).getValues(false)));
                if (check.isEnabled()) register(check);
            } catch (Exception e) {
                plugin.getLogger().warning("Could not load check: " + name);
            }
        });
    }

    public Map<String, Check> getChecks() {
        return checks;
    }
  }
