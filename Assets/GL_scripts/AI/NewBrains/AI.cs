using System.Collections;
using System.Collections.Generic;
using UnityEngine;

public static class AI  {

	public static float Scale = 0.1f;

	static public  List <gen_stat> Gen_stat;

	static public void Start ()
	{
		map.xsize = 500;
		map.ysize = 500;

		/// Здесь выделили память и для тестов присваиваем значения
		/// Потлом присваивать значения будем в другом месте
		ram ();

	}

	static public void ram ()
	{
		Gen_stat = new List<gen_stat> ();
		gen_stat x = new gen_stat();
		x.position_type = 1;
		x.command = 0;
		x.part_num = 3;
		x.ram ();
		Gen_stat.Add (x);
		gen_stat y = new gen_stat();
		y.position_type = 2;
		y.command = 1;
		y.part_num = 4;
		y.ram ();
		Gen_stat.Add (y);
	}
		
		 
}
