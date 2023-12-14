namespace build2
{
  namespace qt
  {
    namespace moc
    {
      template <typename T>
      bool
      pass_moc_options (const T& t, const char* oc)
      {
        // Fall back to qt.moc.auto_preprocessor if the variable is null or
        // undefined.
        //
        lookup l (t[string ("qt.moc.auto_") + oc]);
        return l ? cast<bool> (l)
                 : cast_true<bool> (t["qt.moc.auto_preprocessor"]);
      }
    }
  }
}
