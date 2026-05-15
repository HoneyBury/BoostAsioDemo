package main

import (
    "flag"
    "os"

    clientgoscheme "k8s.io/client-go/kubernetes/scheme"
    ctrl "sigs.k8s.io/controller-runtime"
    "sigs.k8s.io/controller-runtime/pkg/healthz"
    "sigs.k8s.io/controller-runtime/pkg/log/zap"
    metricsserver "sigs.k8s.io/controller-runtime/pkg/metrics/server"

    gatewayv1alpha1 "github.com/honeybury/boostasiodemo/operator/boostgateway-operator/api/v1alpha1"
    "github.com/honeybury/boostasiodemo/operator/boostgateway-operator/internal/controller"
)

func main() {
    var metricsAddr string
    var probeAddr string
    var leaderElect bool

    flag.StringVar(&metricsAddr, "metrics-bind-address", ":8080", "The address the metric endpoint binds to.")
    flag.StringVar(&probeAddr, "health-probe-bind-address", ":8081", "The address the probe endpoint binds to.")
    flag.BoolVar(&leaderElect, "leader-elect", false, "Enable leader election for controller manager.")

    opts := zap.Options{
        Development: true,
    }
    opts.BindFlags(flag.CommandLine)
    flag.Parse()

    ctrl.SetLogger(zap.New(zap.UseFlagOptions(&opts)))

    scheme := clientgoscheme.Scheme
    if err := gatewayv1alpha1.AddToScheme(scheme); err != nil {
        ctrl.Log.WithName("setup").Error(err, "unable to add API to scheme")
        os.Exit(1)
    }

    mgr, err := ctrl.NewManager(ctrl.GetConfigOrDie(), ctrl.Options{
        Scheme: scheme,
        Metrics: metricsserver.Options{
            BindAddress: metricsAddr,
        },
        HealthProbeBindAddress: probeAddr,
        LeaderElection:         leaderElect,
        LeaderElectionID:       "boostgatewaycluster-controller.gateway.boost.io",
    })
    if err != nil {
        ctrl.Log.WithName("setup").Error(err, "unable to start manager")
        os.Exit(1)
    }

    if err := (&controller.BoostGatewayClusterReconciler{
        Client: mgr.GetClient(),
        Scheme: mgr.GetScheme(),
    }).SetupWithManager(mgr); err != nil {
        ctrl.Log.WithName("setup").Error(err, "unable to create controller")
        os.Exit(1)
    }

    if err := mgr.AddHealthzCheck("healthz", healthz.Ping); err != nil {
        ctrl.Log.WithName("setup").Error(err, "unable to set up health check")
        os.Exit(1)
    }
    if err := mgr.AddReadyzCheck("readyz", healthz.Ping); err != nil {
        ctrl.Log.WithName("setup").Error(err, "unable to set up ready check")
        os.Exit(1)
    }

    ctrl.Log.WithName("setup").Info("starting manager")
    if err := mgr.Start(ctrl.SetupSignalHandler()); err != nil {
        ctrl.Log.WithName("setup").Error(err, "problem running manager")
        os.Exit(1)
    }
}
