package {
    import flash.display.MovieClip
    public interface I1 {
        function f()
    }
    public class TestI1 implements I1{
        public function f() {
            trace("ok");
        }
    }
    public class Main extends flash.display.MovieClip {
        function Main() {
            var x:I1 = new TestI1();
            x.f();
        }
    }
    public interface I2 {
    }
}